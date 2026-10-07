"""Headless Indigo attachment. Call start() from an Indigo Server Startup action.

All Indigo operations run on one receiver thread. Importing has no side effects.
See remote-states.md for configuration, wire format, and lifecycle.
"""
import hmac
import json
import math
import re
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

_service = None
_lifecycle_lock = threading.Lock()


def scalar(value, kind):
    if value is None:
        return 'unknown'
    if kind == 'bool' and type(value) is bool:
        return 'true' if value else 'false'
    if kind == 'number' and type(value) in (int, float) and math.isfinite(value):
        return str(value)
    if kind == 'string' and isinstance(value, str) and len(value) <= 256:
        return value
    raise ValueError('Invalid state value type')


class StateStore:
    def __init__(self, indigo, config, registry_path):
        self.indigo, self.config = indigo, config
        self.path = Path(registry_path)
        self.registry = json.loads(self.path.read_text()) if self.path.exists() else {'version': 1, 'variables': {}}
        if self.registry.get('version') != 1 or not isinstance(self.registry.get('variables'), dict):
            raise ValueError('Invalid variable registry; refusing to discard saved IDs')
        self.last_seen = {}
        self.timeouts = {}
        self.invalid_timeouts = {}
        self.verbosity = {}
        self.invalid_verbosity = {}
        self.folder_id = None
        names = set()
        for source in config['sources'].values():
            name = source.get('timeout_variable', source['prefix'] + '_Set_HeartbeatTimeout')
            if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name) or name in names:
                raise ValueError('Invalid or duplicate timeout variable name')
            names.add(name)
            verbosity_name = source['prefix'] + '_Set_Verbosity'
            if verbosity_name in names:
                raise ValueError('Duplicate verbosity variable name')
            names.add(verbosity_name)
        for source in config['sources'].values():
            if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', source['prefix']):
                raise ValueError('Invalid source prefix')
            if not isinstance(source.get('token'), str) or len(source['token']) < 24:
                raise ValueError('Each source needs a token of at least 24 characters')
            if not math.isfinite(source.get('offline_after', 90)) or source.get('offline_after', 90) <= 0:
                raise ValueError('offline_after must be positive')
            for key, field in source['fields'].items():
                if key in ('LastSeen', 'Online', 'HeartbeatTimeout', 'Verbosity') or field['type'] not in ('bool', 'number', 'string'):
                    raise ValueError('Invalid field mapping')
                if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', field['name']):
                    raise ValueError('Invalid variable suffix')
            field_names = []
            for spec in source['fields'].values():
                prefix = spec.get('prefix', source['prefix'])
                if not isinstance(prefix, str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', prefix):
                    raise ValueError('Invalid field prefix')
                field_names.append(prefix + '_' + spec['name'])
            field_names += [source['prefix'] + '_' + suffix for suffix in ('LastSeen', 'Online')]
            for name in field_names:
                if name in names:
                    raise ValueError('Duplicate variable name: ' + name)
                names.add(name)

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix('.tmp')
        tmp.write_text(json.dumps(self.registry, indent=2) + '\n')
        tmp.replace(self.path)

    def setup(self):
        folders = self.indigo.variables.folders
        saved = self.registry.get('folder_id')
        if saved in folders:
            self.folder_id = saved
        elif self.config['folder'] in folders:
            self.folder_id = folders[self.config['folder']].id
        else:
            self.folder_id = self.indigo.variables.folder.create(self.config['folder']).id
        self.registry['folder_id'] = self.folder_id
        for source_id, source in self.config['sources'].items():
            for field, spec in source['fields'].items():
                self.ensure(source_id, field, spec['name'])
            self.ensure(source_id, 'LastSeen', 'LastSeen')
            self.ensure(source_id, 'Online', 'Online')
            self.update(source_id, 'Online', 'false')
            self.timeouts[source_id] = source.get('offline_after', 90)
            self.read_timeout(source_id)
            self.read_verbosity(source_id)
        self.save()

    def ensure(self, source_id, field, suffix, name=None, initial='unknown'):
        source_ids = self.registry['variables'].setdefault(source_id, {})
        saved = source_ids.get(field)
        if saved in self.indigo.variables:
            return saved  # Renames and folder moves do not break the mapping.
        source = self.config['sources'][source_id]
        prefix = source['fields'].get(field, {}).get('prefix', source['prefix'])
        name = name or prefix + '_' + suffix
        if name in self.indigo.variables:
            variable = self.indigo.variables[name]
            if variable.folderId != self.folder_id:
                raise ValueError('Name already used outside remote states folder: ' + name)
        else:
            variable = self.indigo.variable.create(name, value=initial, folder=self.folder_id)
        source_ids[field] = variable.id
        self.save()
        return variable.id

    def update(self, source_id, field, value):
        suffix = field if field in ('LastSeen', 'Online') else self.config['sources'][source_id]['fields'][field]['name']
        variable_id = self.ensure(source_id, field, suffix)
        if self.indigo.variables[variable_id].value != value:
            self.indigo.variable.updateValue(variable_id, value=value)

    def validate(self, source_id, states):
        if not isinstance(states, dict):
            raise ValueError('states must be an object')
        mapping = self.config['sources'][source_id]['fields']
        values = {key: scalar(value, mapping[key]['type']) for key, value in states.items() if key in mapping}
        if not values:
            raise ValueError('No mapped states in request')
        return values

    def validate_events(self, events):
        if not isinstance(events, list) or len(events) > 16:
            raise ValueError("Invalid events")
        for event in events:
            if (not isinstance(event, dict)
                    or not isinstance(event.get("id"), str)
                    or not re.fullmatch(r"[a-f0-9]+-[0-9]+", event["id"])
                    or len(event["id"]) > 64
                    or not isinstance(event.get("message"), str)
                    or len(event["message"]) > 256
                    or any(ord(c) < 32 for c in event["message"])):
                raise ValueError("Invalid event")
        return events

    def summarize_event(self, source_id, event):
        """Condense gateway diagnostics without hiding uncommanded movement."""
        context = self.registry.setdefault('event_context', {}).setdefault(source_id, {})
        boot, sequence = event['id'].rsplit('-', 1)
        sequence = int(sequence)
        if context.get('boot') != boot or context.get('sequence', sequence - 1) != sequence - 1:
            context.clear()  # Do not attribute a result across a reboot or missing events.
        context.update(boot=boot, sequence=sequence)
        message = event['message']
        match = re.fullmatch(r'(\d+) ms: (.*)', message)
        if not match:
            return message
        stamp, text = int(match[1]), match[2]
        request = re.fullmatch(r'(OPEN|CLOSE) request \[(web|API)\]: (queued|rejected)', text)
        if request:
            action, origin, result = request.groups()
            if result == 'rejected':
                return text
            context['request'] = [action, origin]
            return None
        result = re.fullmatch(r'BLE result: (\w+); position: (\w+)', text)
        if result:
            outcome, position = result.groups()
            if outcome in ('none', 'queued', 'sending'):
                return None
            request = context.pop('request', None)
            if outcome == 'confirmed':
                context['confirmation'] = [stamp, position]
                if request and position == ('open' if request[0] == 'OPEN' else 'closed'):
                    if request[1] == 'API':
                        return None  # The Indigo caller already logs acceptance.
                    return 'Feeder ' + position + ' [web]'
            return text  # Always retain failures and unattributed completions.
        position = re.fullmatch(
            r'Observed door position: (.*?) -> (\w+); requested: (\w+); result: (\w+)', text)
        if position:
            before, after, requested, outcome = position.groups()
            confirmation = context.pop('confirmation', None)
            if (confirmation and after == confirmation[1]
                    and 0 <= stamp - confirmation[0] <= 100):
                return None  # Companion position event for the same completion.
            if after == 'unknown' and outcome in ('queued', 'sending'):
                return None
            return text
        return text

    def read_verbosity(self, source_id):
        variable_id = self.ensure(source_id, 'Verbosity', 'Set_Verbosity', initial='normal')
        raw = self.indigo.variables[variable_id].value
        value = raw.strip().lower()
        if value in ('normal', 'verbose'):
            self.verbosity[source_id] = value
            self.invalid_verbosity.pop(source_id, None)
        elif self.invalid_verbosity.get(source_id) != raw:
            self.indigo.server.log(
                self.indigo.variables[variable_id].name + ': use normal or verbose; keeping '
                + self.verbosity.get(source_id, 'normal') + '.', type='Remote States', isError=True)
            self.invalid_verbosity[source_id] = raw
        return self.verbosity.get(source_id, 'normal')

    def apply_events(self, source_id, events):
        verbose = self.read_verbosity(source_id) == 'verbose'
        seen = self.registry.setdefault("event_ids", {}).setdefault(source_id, [])
        for event in events:
            if event["id"] in seen:
                continue
            message = self.summarize_event(source_id, event)
            if verbose:
                message = event["message"]
            if message is not None:
                self.indigo.server.log(
                    self.config["sources"][source_id]["prefix"] + ": " + message,
                    type="Remote States")
            seen.append(event["id"])
            del seen[:-64]
            self.save()

    def apply(self, source_id, values):
        for key, value in values.items():
            self.update(source_id, key, value)
        self.update(source_id, 'LastSeen', datetime.now(timezone.utc).isoformat(timespec='seconds'))
        self.update(source_id, 'Online', 'true')
        self.last_seen[source_id] = time.monotonic()

    def read_timeout(self, source_id):
        source = self.config['sources'][source_id]
        name = source.get('timeout_variable', source['prefix'] + '_Set_HeartbeatTimeout')
        variable_id = self.ensure(source_id, 'HeartbeatTimeout', 'Set_HeartbeatTimeout',
                                  name=name, initial=str(self.timeouts[source_id]))
        raw = self.indigo.variables[variable_id].value
        try:
            value = float(raw)
            if not math.isfinite(value) or value <= 0:
                raise ValueError()
        except (ValueError, TypeError):
            if self.invalid_timeouts.get(source_id) != raw:
                self.indigo.server.log(
                    name + ': enter a positive, finite timeout in seconds; keeping '
                    + str(self.timeouts[source_id]) + ' seconds.',
                    type='Remote States', isError=True)
                self.invalid_timeouts[source_id] = raw
        else:
            self.timeouts[source_id] = value
            self.invalid_timeouts.pop(source_id, None)
        return self.timeouts[source_id]

    def expire(self):
        for source_id in self.config['sources']:
            self.read_timeout(source_id)
            self.read_verbosity(source_id)
        for source_id, received in list(self.last_seen.items()):
            if time.monotonic() - received > self.timeouts[source_id]:
                self.update(source_id, 'Online', 'false')
                del self.last_seen[source_id]


class Receiver(HTTPServer):
    allow_reuse_address = True
    def get_request(self):
        sock, address = super().get_request()
        sock.settimeout(2)
        return sock, address


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, status, message):
        body = json.dumps({'message': message}).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.close_connection = True
        try:
            self.wfile.write(body)
        except OSError:
            pass

    def do_POST(self):
        if self.path != '/remote-states/v1':
            return self.reply(404, 'Unknown endpoint')
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if self.headers.get('Transfer-Encoding') or not 0 < length <= 16384:
                return self.reply(413, 'Invalid body length')
            if self.headers.get_content_type() != 'application/json':
                return self.reply(415, 'Expected application/json')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict) or data.get('version') != 1 or not isinstance(data.get('source'), str):
                return self.reply(400, 'Invalid envelope')
            store = self.server.store
            source_id = data['source']
            source = store.config['sources'].get(source_id)
            supplied = self.headers.get('X-Remote-States-Key', '')
            if not source or not hmac.compare_digest(supplied.encode(), source['token'].encode()):
                return self.reply(403, 'Unknown source or invalid key')
            values = store.validate(source_id, data.get('states'))
            events = store.validate_events(data.get('events', []))
        except (ValueError, TypeError, UnicodeError):
            return self.reply(400, 'Invalid payload')
        except OSError:
            return self.reply(408, 'Request timed out')
        try:
            store.apply(source_id, values)
            store.apply_events(source_id, events)
        except Exception as exc:
            self.server.report('Variable update failed: ' + str(exc))
            return self.reply(503, 'Variable update failed')
        self.reply(200, 'States updated')


class Service:
    def __init__(self, indigo, config, registry_path):
        self.indigo = indigo
        self.store = StateStore(indigo, config, registry_path)
        self.server = Receiver((config.get('bind', '0.0.0.0'), config.get('port', 8786)), Handler)
        self.server.store, self.server.report = self.store, self.report
        self.server.timeout = 1
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self.run, name='Indigo remote states', daemon=True)

    def report(self, text, error=True):
        self.indigo.server.log(text, type='Remote States', isError=error)

    def run(self):
        try:
            self.store.setup()
            self.report('Receiver started on port ' + str(self.server.server_port), False)
            while not self.stopping.is_set():
                self.server.handle_request()
                self.store.expire()
        except Exception as exc:
            self.report('Receiver stopped: ' + str(exc))
        finally:
            self.server.server_close()
            for source in self.store.last_seen:
                try:
                    self.store.update(source, 'Online', 'false')
                except Exception:
                    pass

    def stop(self):
        self.stopping.set()
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            raise RuntimeError('Receiver has not stopped yet')


def start(config_path=None):
    """Idempotent startup entry point for an embedded Indigo Python action."""
    global _service
    import indigo
    with _lifecycle_lock:
        if _service and _service.thread.is_alive():
            return
        path = Path(config_path) if config_path else Path(__file__).with_name('remote_states_config.json')
        config = json.loads(path.read_text())
        registry_path = path.parent / 'remote_states_ids.json'
        _service = Service(indigo, config, registry_path)
        _service.thread.start()


def stop():
    global _service
    with _lifecycle_lock:
        if _service:
            _service.stop()
            _service = None

import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('remote_states', Path(__file__).parents[1] / 'remote_states.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Items(dict):
    def __contains__(self, key):
        return super().__contains__(key) or any(x.name == key for x in self.values())
    def __getitem__(self, key):
        if super().__contains__(key): return super().__getitem__(key)
        return next(x for x in self.values() if x.name == key)

class TestStates(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.variables = Items()
        self.variables.folders = Items()
        self.updates = []
        self.logs = []
        def folder(name):
            f=SimpleNamespace(id=100,name=name);self.variables.folders[f.id]=f;return f
        def create(name,value,folder):
            v=SimpleNamespace(id=len(self.variables)+1,name=name,value=value,folderId=folder)
            self.variables[v.id]=v;return v
        def update(id,value):
            self.updates.append((id,value));self.variables[id].value=value
        self.variables.folder = SimpleNamespace(create=folder)
        self.indigo = SimpleNamespace(server=SimpleNamespace(log=lambda text, **kwargs: self.logs.append(text)),variables=self.variables,variable=SimpleNamespace(create=create,updateValue=update))
        self.config=json.loads((Path(__file__).parents[1]/'remote_states_config.example.json').read_text())
        self.source=next(iter(self.config['sources']))
        self.registry=Path(self.temp.name)/'ids.json'
        self.store=m.StateStore(self.indigo,self.config,self.registry);self.store.setup()

    def test_events_deduplicate_retry_and_receiver_restart(self):
        events = self.store.validate_events([
            {'id': 'abcd-1', 'message': 'OPEN request [web]: queued'},
            {'id': 'abcd-2', 'message': 'Observed door position: open -> closed'},
        ])
        self.store.apply_events(self.source, events)
        self.store.apply_events(self.source, events)
        self.assertEqual(len(self.logs), 2)
        restarted = m.StateStore(self.indigo, self.config, self.registry)
        restarted.setup()
        restarted.apply_events(self.source, events)
        self.assertEqual(len(self.logs), 2)
        restarted.apply_events(self.source, [{'id': 'eeee-1', 'message': 'Gateway started'}])
        self.assertEqual(len(self.logs), 3)

    def test_concise_command_logs_and_unexpected_closure(self):
        def event(n, text, stamp=None):
            return {'id': 'abcd-' + str(n), 'message': str(stamp or n * 100) + ' ms: ' + text}
        for origin in ('API', 'web'):
            self.logs.clear()
            self.store.registry.pop('event_ids', None)
            self.store.registry.pop('event_context', None)
            events = [
                event(1, 'OPEN request [' + origin + ']: queued'),
                event(2, 'BLE result: queued; position: closed'),
                event(3, 'BLE result: sending; position: unknown'),
                event(4, 'Observed door position: closed -> unknown; requested: open; result: sending'),
                event(5, 'BLE result: confirmed; position: open'),
                event(6, 'Observed door position: unknown -> open; requested: open; result: confirmed', 500),
            ]
            self.store.apply_events(self.source, events[:4])
            restarted = m.StateStore(self.indigo, self.config, self.registry)
            restarted.setup()
            restarted.apply_events(self.source, events)
            self.assertEqual(len(self.logs), 0 if origin == 'API' else 1)
            if origin == 'web': self.assertIn('Feeder open [web]', self.logs[0])
            restarted.apply_events(self.source, [event(7,
                'Observed door position: open -> closed; requested: open; result: confirmed')])
            self.assertIn('open -> closed', self.logs[-1])

    def test_failures_and_missing_event_context_remain_visible(self):
        events = [
            {'id': 'abcd-1', 'message': '100 ms: CLOSE request [API]: rejected'},
            {'id': 'abcd-2', 'message': '200 ms: OPEN request [API]: queued'},
            {'id': 'abcd-3', 'message': '300 ms: BLE result: failed; position: unknown'},
            {'id': 'abcd-5', 'message': '500 ms: BLE result: confirmed; position: open'},
        ]
        self.store.apply_events(self.source, events)
        self.assertEqual(len(self.logs), 3)
        self.assertIn('rejected', self.logs[0])
        self.assertIn('failed', self.logs[1])
        self.assertIn('confirmed', self.logs[2])

    def test_live_verbosity_setting_validation_and_restart(self):
        setting = self.variables['ChickenFeeder_Set_Verbosity']
        self.assertEqual(setting.value, 'normal')
        event = lambda n: {'id': 'abcd-' + str(n), 'message': '100 ms: BLE result: sending; position: unknown'}
        self.store.apply_events(self.source, [event(1)])
        self.assertEqual(self.logs, [])
        setting.value = 'verbose'
        self.store.apply_events(self.source, [event(2)])
        self.assertEqual(len(self.logs), 1)
        restarted = m.StateStore(self.indigo, self.config, self.registry)
        restarted.setup()
        self.assertEqual(restarted.read_verbosity(self.source), 'verbose')
        setting.value = 'invalid'
        restarted.read_verbosity(self.source)
        count = len(self.logs)
        self.assertEqual(restarted.read_verbosity(self.source), 'verbose')
        self.assertEqual(len(self.logs), count)
        setting.value = 'normal'
        restarted.apply_events(self.source, [event(3)])
        self.assertEqual(len(self.logs), count)
        setting.name = 'Set_Verbosity'
        self.assertEqual(restarted.read_verbosity(self.source), 'normal')

    def test_invalid_events_rejected(self):
        for events in ({}, [None], [{'id': 'bad', 'message': 'x'}],
                       [{'id': 'abcd-1', 'message': 'x\nforged'}],
                       [{'id': 'abcd-1', 'message': 'x' * 257}],
                       [{'id': 'abcd-1', 'message': 'x'}] * 17):
            with self.assertRaises(ValueError):
                self.store.validate_events(events)

    def test_rename_and_restart_preserve_ids_and_values(self):
        self.store.apply(self.source,self.store.validate(self.source,{'physical_state':'open'}))
        id=self.store.registry['variables'][self.source]['physical_state']
        self.variables[id].name='Renamed';self.variables[id].folderId=999
        original_ids=set(self.variables)
        restarted=m.StateStore(self.indigo,self.config,self.registry);restarted.setup()
        self.assertEqual(restarted.registry['variables'][self.source]['physical_state'],id)
        self.assertEqual(self.variables[id].value,'open')
        self.assertEqual(set(self.variables),original_ids)

    def test_validation_unknown_and_unchanged(self):
        values=self.store.validate(self.source,{'ble_connected':True,'chip_temperature_c':None,'unexpected':12})
        self.assertEqual(values,{'ble_connected':'true','chip_temperature_c':'unknown'})
        self.store.apply(self.source,values);self.updates.clear()
        self.store.update(self.source,'ble_connected','true');self.assertEqual(self.updates,[])
        with self.assertRaises(ValueError):self.store.validate(self.source,{'ble_connected':'true'})
        with self.assertRaises(ValueError):self.store.validate(self.source,{'chip_temperature_c':float('nan')})

    def test_offline_and_collision(self):
        self.store.apply(self.source,{'physical_state':'closed'})
        self.store.last_seen[self.source]-=100
        self.store.expire()
        self.assertEqual(self.variables['ChickenFeeder_Online'].value,'false')
        self.assertEqual(self.variables['ChickenFeeder_DoorState'].value,'closed')
        self.config['sources']['other']=dict(self.config['sources'][self.source])
        with self.assertRaises(ValueError):m.StateStore(self.indigo,self.config,self.registry)

    def test_live_timeout_and_restart(self):
        setting = self.variables['Set_HeartbeatTimeout']
        self.assertEqual(setting.value, '90')
        self.assertEqual(setting.folderId, self.store.folder_id)
        setting.value = '180'
        self.store.apply(self.source, {'physical_state': 'closed'})
        self.store.last_seen[self.source] -= 100
        self.store.expire()
        self.assertEqual(self.variables['ChickenFeeder_Online'].value, 'true')
        setting.value = '60'
        self.store.expire()
        self.assertEqual(self.variables['ChickenFeeder_Online'].value, 'false')
        restarted = m.StateStore(self.indigo, self.config, self.registry)
        restarted.setup()
        self.assertEqual(setting.value, '60')
        self.assertEqual(restarted.timeouts[self.source], 60)

    def test_invalid_timeout_retains_last_valid_and_warns_once(self):
        setting = self.variables['Set_HeartbeatTimeout']
        setting.value = '120'
        self.store.expire()
        for bad in ('no', '0', '-1', 'nan', 'inf'):
            setting.value = bad
            self.store.expire()
            count = len(self.logs)
            self.store.expire()
            self.assertEqual(len(self.logs), count)
            self.assertEqual(self.store.timeouts[self.source], 120)
        self.assertEqual(len(self.logs), 5)
        restarted = m.StateStore(self.indigo, self.config, self.registry)
        restarted.setup()
        self.assertEqual(setting.value, 'inf')
        self.assertEqual(restarted.timeouts[self.source], 90)

    def test_corrupt_registry_is_not_overwritten(self):
        self.registry.write_text('{bad')
        with self.assertRaises(ValueError):m.StateStore(self.indigo,self.config,self.registry)

if __name__=='__main__':unittest.main()

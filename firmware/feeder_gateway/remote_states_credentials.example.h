#pragma once
// Optional Indigo publisher. Empty endpoint disables publication.
const char kStateEndpoint[] = ""; // e.g. http://INDIGO-SERVER:8786/remote-states/v1
const char kStateSource[] = "go-feeder-gateway";
const char kStateToken[] = ""; // Generate >=24 characters; match receiver config.

#include "../feeder_gateway/LatestCommand.h"
#include <assert.h>
int main() {
 LatestCommand q; bool target;
 q.request(true); assert(q.take(target) && target);
 q.request(false); q.request(true); q.request(false);
 assert(!q.take(target)); q.finish(true);
 assert(q.busy() && q.result==1);
 assert(q.take(target) && !target); q.finish(true);
 assert(!q.busy() && q.result==3 && !q.take(target));
 // Last request matches active state: obsolete opposite request must disappear.
 q.request(true); assert(q.take(target) && target);
 q.request(false); q.request(true); q.finish(true);
 assert(q.take(target) && target); q.finish(true);
 // Failure must not discard a newer desired state.
 q.request(false); assert(q.take(target)); q.request(true); q.finish(false);
 assert(q.take(target) && target); q.finish(true);
 // Offline commands must not replay after reconnection.
 q.request(false); q.disconnect(); assert(!q.busy() && !q.take(target));
 // Finish then request: a new request remains queued.
 q.request(false); assert(q.take(target)); q.finish(true); q.request(true);
 assert(q.busy() && q.result==1 && q.take(target) && target);
}

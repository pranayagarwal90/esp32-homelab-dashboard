#include "PhotoRequestTracker.h"
#include <assert.h>
#include <stdio.h>

using P = PhotoPurpose;
using A = PhotoAction;

int main() {
  PhotoRequestTracker t;
  assert(!t.needsRequest() && !t.busy());

  // Page entry -> request #1 in flight.
  uint32_t a = t.want(P::Page);
  assert(t.needsRequest());
  t.sent();
  assert(t.busy() && !t.needsRequest());

  // NEXT twice while #1 downloads: only the latest desire survives, and no
  // second request is sent while the slot is busy.
  t.want(P::Page);
  uint32_t c = t.want(P::Page);
  assert(c != a && !t.needsRequest());

  // #1 returns late: stale, discarded, and the slot is free for #3.
  assert(!t.complete(a));
  assert(decidePhotoAction(false, true, false, t.wanted(), 5) == A::Discard);
  assert(t.needsRequest());
  t.sent();
  assert(t.complete(c)); // Latest result is current.
  assert(decidePhotoAction(false, true, true, P::Page, 5) == A::ShowPagePhoto);
  t.fulfil();
  assert(!t.needsRequest());

  // PREV then leave Photos before the result: cancelled, never drawn.
  uint32_t d = t.want(P::Page);
  t.sent();
  t.cancel(P::Page);
  assert(!t.complete(d));
  assert(!t.needsRequest());

  // Screensaver request, user wakes (cancel), result discarded.
  uint32_t e = t.want(P::Screensaver);
  t.sent();
  t.cancel(P::Screensaver);
  assert(!t.complete(e));
  assert(decidePhotoAction(false, true, false, P::None, 5) == A::Discard);

  // Cancelling a different purpose leaves the desire alone.
  uint32_t f = t.want(P::Screensaver);
  t.cancel(P::Page);
  assert(t.wanted() == P::Screensaver && t.wantedGeneration() == f);

  // Waking into Photos supersedes the screensaver desire.
  t.sent();
  uint32_t g = t.want(P::Page);
  assert(!t.complete(f));
  assert(t.needsRequest() && t.wantedGeneration() == g);
  t.sent();

  // A failed stale result leaves the current image alone (Discard); a failed
  // current one shows the old error screens.
  assert(decidePhotoAction(false, false, false, P::Page, 5) == A::Discard);
  assert(t.complete(g));
  assert(decidePhotoAction(false, false, true, P::Page, 5) == A::ShowPageError);
  assert(decidePhotoAction(false, false, true, P::Screensaver, 5) == A::ScreensaverFallback);
  assert(decidePhotoAction(false, true, true, P::Screensaver, 5) == A::ShowScreensaverPhoto);
  t.fulfil();

  // List stage: a current list keeps the desire open for the image request.
  uint32_t h = t.want(P::Page);
  t.sent();
  assert(t.complete(h));
  assert(decidePhotoAction(true, true, true, P::Page, 3) == A::RequestImage);
  assert(t.needsRequest() && t.wantedGeneration() == h);
  // Failed or empty list -> old "no photos" outcomes.
  assert(decidePhotoAction(true, false, true, P::Page, 0) == A::ShowNoPhotos);
  assert(decidePhotoAction(true, true, true, P::Page, 0) == A::ShowNoPhotos);
  assert(decidePhotoAction(true, false, true, P::Screensaver, 0) == A::ScreensaverFallback);
  t.fulfil();

  // Boot prefetch is untracked: never current, even with no desire.
  t.sent();
  assert(!t.complete(PhotoRequestTracker::UNTRACKED));
  t.want(P::Page);
  t.sent();
  assert(!t.complete(PhotoRequestTracker::UNTRACKED));
  assert(t.needsRequest()); // Desire still pending after the prefetch.

  puts("Photo request tests passed: supersession, stale/cancelled results, wake, list stage, prefetch");
}

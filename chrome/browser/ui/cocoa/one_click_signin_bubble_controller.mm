// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/one_click_signin_bubble_controller.h"

#include "base/logging.h"
#import "chrome/browser/ui/cocoa/browser_window_controller.h"
#import "chrome/browser/ui/cocoa/info_bubble_view.h"
#import "chrome/browser/ui/cocoa/toolbar/toolbar_controller.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#import "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/l10n/l10n_util.h"
#import "ui/base/l10n/l10n_util_mac.h"

namespace {

// Shift the origin of |view|'s frame by the given amount in the
// positive y direction (up).
void ShiftOriginY(NSView* view, CGFloat amount) {
  NSPoint origin = [view frame].origin;
  origin.y += amount;
  [view setFrameOrigin:origin];
}

}  // namespace

@implementation OneClickSigninBubbleController

- (id)initWithBrowserWindowController:(BrowserWindowController*)controller
                  start_sync_callback:(const BrowserWindow::StartSyncCallback&)
                      start_sync_callback {
  NSWindow* parentWindow = [controller window];

  // Set the anchor point to right below the wrench menu.
  NSView* wrenchButton = [[controller toolbarController] wrenchButton];
  const NSRect bounds = [wrenchButton bounds];
  NSPoint anchorPoint = NSMakePoint(NSMidX(bounds), NSMaxY(bounds));
  anchorPoint = [wrenchButton convertPoint:anchorPoint toView:nil];
  anchorPoint = [parentWindow convertBaseToScreen:anchorPoint];

  if (self = [super initWithWindowNibPath:@"OneClickSigninBubble"
                             parentWindow:parentWindow
                               anchoredAt:anchorPoint]) {
    start_sync_callback_ = start_sync_callback;
    DCHECK(!start_sync_callback_.is_null());
  }
  return self;
}

- (IBAction)ok:(id)sender {
  start_sync_callback_.Run(
      OneClickSigninSyncStarter::SYNC_WITH_DEFAULT_SETTINGS);
  [self close];
}

- (IBAction)onClickLearnMoreLink:(id)sender {
  // TODO(akalin): this method should be removed once the UI elements are
  // removed.
}

- (IBAction)onClickAdvancedLink:(id)sender {
  start_sync_callback_.Run(
      OneClickSigninSyncStarter::CONFIGURE_SYNC_FIRST);
  [self close];
}

// TODO(rogerta): if the bubble is closed without interaction, need to call
// the callback with argument set to SYNC_WITH_DEFAULT_SETTINGS.

- (void)awakeFromNib {
  [super awakeFromNib];

  // Set the message text manually, since we have to interpolate the
  // product name.
  NSString* message =
      l10n_util::GetNSStringF(
          IDS_SYNC_PROMO_NTP_BUBBLE_MESSAGE,
          l10n_util::GetStringUTF16(IDS_SHORT_PRODUCT_NAME));
  [messageField_ setStringValue:message];

  // Lay out the text controls from the bottom up.
  CGFloat totalYOffset = 0.0;

  totalYOffset +=
      [GTMUILocalizerAndLayoutTweaker sizeToFitView:advancedLink_].height;

  ShiftOriginY(learnMoreLink_, totalYOffset);
  totalYOffset +=
      [GTMUILocalizerAndLayoutTweaker sizeToFitView:learnMoreLink_].height;

  ShiftOriginY(messageField_, totalYOffset);
  totalYOffset +=
      [GTMUILocalizerAndLayoutTweaker
          sizeToFitFixedWidthTextField:messageField_];

  NSSize delta = NSMakeSize(0.0, totalYOffset);

  // Resize bubble and window to hold the controls.
  [GTMUILocalizerAndLayoutTweaker
      resizeViewWithoutAutoResizingSubViews:[self bubble]
                                      delta:delta];
  [GTMUILocalizerAndLayoutTweaker
      resizeWindowWithoutAutoResizingSubViews:[self window]
                                        delta:delta];
}

@end  // OneClickSigninBubbleController

// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_USER_MANAGER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_USER_MANAGER_VIEW_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_window.h"
#include "ui/views/window/dialog_delegate.h"

class AutoKeepAlive;

namespace views {
class WebView;
}

// Dialog widget that contains the Desktop User Manager webui.
class UserManagerView : public views::DialogDelegateView {
 public:
  // Do not call directly. To display the User Manager, use UserManager::Show().
  UserManagerView();

  // Creates a new UserManagerView instance for the |guest_profile| and
  // shows the |url|.
  static void OnGuestProfileCreated(scoped_ptr<UserManagerView> instance,
                                    Profile* guest_profile,
                                    const std::string& url);

 private:
  ~UserManagerView() override;

  friend struct base::DefaultDeleter<UserManagerView>;

  // Creates dialog and initializes UI.
  void Init(Profile* guest_profile, const GURL& url);

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  gfx::Size GetPreferredSize() const override;

  // views::DialogDelegateView:
  bool CanResize() const override;
  bool CanMaximize() const override;
  bool CanMinimize() const override;
  base::string16 GetWindowTitle() const override;
  int GetDialogButtons() const override;
  void WindowClosing() override;
  bool UseNewStyleForThisDialog() const override;

  views::WebView* web_view_;

  scoped_ptr<AutoKeepAlive> keep_alive_;

  DISALLOW_COPY_AND_ASSIGN(UserManagerView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_USER_MANAGER_VIEW_H_

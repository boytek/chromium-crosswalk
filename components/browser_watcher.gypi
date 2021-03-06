# Copyright (c) 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'conditions': [
  ['OS=="win"', {
      'targets': [
          {
            # This is a separate lib to minimize the dependencies for its
            # hosting binary "chrome_watcher.dll".
            'target_name': 'browser_watcher',
            'type': 'static_library',
            'sources': [
              'browser_watcher/exit_code_watcher_win.cc',
              'browser_watcher/exit_code_watcher_win.h',
              'browser_watcher/exit_funnel_win.cc',
              'browser_watcher/exit_funnel_win.h',
            ],
            'dependencies': [
              '../base/base.gyp:base',
            ],
          },
          {
            # Users of the watcher link this target.
            'target_name': 'browser_watcher_client',
            'type': 'static_library',
            'sources': [
              'browser_watcher/watcher_client_win.cc',
              'browser_watcher/watcher_client_win.h',
              'browser_watcher/watcher_main_api_win.cc',
              'browser_watcher/watcher_main_api_win.h',
              'browser_watcher/watcher_metrics_provider_win.cc',
              'browser_watcher/watcher_metrics_provider_win.h',
            ],
            'dependencies': [
              'browser_watcher',
              '../base/base.gyp:base',
            ],
          },
        ],
      }
    ],
  ],
}
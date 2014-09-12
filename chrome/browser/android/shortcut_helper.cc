// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/shortcut_helper.h"

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/basictypes.h"
#include "base/location.h"
#include "base/strings/string16.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/threading/worker_pool.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/frame_navigate_params.h"
#include "jni/ShortcutHelper_jni.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_analysis.h"
#include "ui/gfx/favicon_size.h"
#include "url/gurl.h"

jlong Initialize(JNIEnv* env, jobject obj, jlong tab_android_ptr) {
  TabAndroid* tab = reinterpret_cast<TabAndroid*>(tab_android_ptr);

  ShortcutHelper* shortcut_helper =
      new ShortcutHelper(env, obj, tab->web_contents());
  shortcut_helper->Initialize();

  return reinterpret_cast<intptr_t>(shortcut_helper);
}

ShortcutHelper::ShortcutHelper(JNIEnv* env,
                               jobject obj,
                               content::WebContents* web_contents)
    : WebContentsObserver(web_contents),
      java_ref_(env, obj),
      url_(web_contents->GetURL()),
      shortcut_type_(BOOKMARK) {
}

void ShortcutHelper::Initialize() {
  // Send a message to the renderer to retrieve information about the page.
  Send(new ChromeViewMsg_RetrieveWebappInformation(routing_id(), url_));
}

ShortcutHelper::~ShortcutHelper() {
}

void ShortcutHelper::OnDidRetrieveWebappInformation(
    bool success,
    bool is_mobile_webapp_capable,
    bool is_apple_mobile_webapp_capable,
    const GURL& expected_url) {
  // This should silently fail.
  if (!success) {
    LOG(ERROR) << "Failed to parse webpage.";
  } else if (expected_url != url_) {
    LOG(ERROR) << "Unexpected URL returned.";
  }

  if (is_apple_mobile_webapp_capable && !is_mobile_webapp_capable) {
    shortcut_type_ = APP_SHORTCUT_APPLE;
  } else if (is_apple_mobile_webapp_capable || is_mobile_webapp_capable) {
    shortcut_type_ = APP_SHORTCUT;
  } else {
    shortcut_type_ = BOOKMARK;
  }

  title_ = web_contents()->GetTitle();

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_obj = java_ref_.get(env);
  ScopedJavaLocalRef<jstring> j_title =
      base::android::ConvertUTF16ToJavaString(env, title_);

  Java_ShortcutHelper_onInitialized(env, j_obj.obj(), j_title.obj());
}

void ShortcutHelper::TearDown(JNIEnv*, jobject) {
  Destroy();
}

void ShortcutHelper::Destroy() {
  delete this;
}

void ShortcutHelper::AddShortcut(
    JNIEnv* env,
    jobject obj,
    jstring jtitle,
    jint launcher_large_icon_size) {
  base::string16 title = base::android::ConvertJavaStringToUTF16(env, jtitle);
  if (!title.empty())
    title_ = title;

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());

  // Grab the best, largest icon we can find to represent this bookmark.
  // TODO(dfalcantara): Try combining with the new BookmarksHandler once its
  //                    rewrite is further along.
  std::vector<int> icon_types;
  icon_types.push_back(favicon_base::FAVICON);
  icon_types.push_back(favicon_base::TOUCH_PRECOMPOSED_ICON |
                       favicon_base::TOUCH_ICON);
  FaviconService* favicon_service = FaviconServiceFactory::GetForProfile(
      profile, Profile::EXPLICIT_ACCESS);

  // Using favicon if its size is not smaller than platform required size,
  // otherwise using the largest icon among all avaliable icons.
  int threshold_to_get_any_largest_icon = launcher_large_icon_size_ - 1;
  favicon_service->GetLargestRawFaviconForPageURL(url_, icon_types,
      threshold_to_get_any_largest_icon,
      base::Bind(&ShortcutHelper::FinishAddingShortcut,
                 base::Unretained(this)),
      &cancelable_task_tracker_);
}

void ShortcutHelper::FinishAddingShortcut(
    const favicon_base::FaviconRawBitmapResult& bitmap_result) {
  icon_ = bitmap_result;

  // Stop observing so we don't get destroyed while doing the last steps.
  Observe(NULL);

  base::WorkerPool::PostTask(
      FROM_HERE,
      base::Bind(&ShortcutHelper::AddShortcutInBackground,
                 url_,
                 title_,
                 shortcut_type_,
                 icon_),
      true);

  Destroy();
}

bool ShortcutHelper::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;

  IPC_BEGIN_MESSAGE_MAP(ShortcutHelper, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_DidRetrieveWebappInformation,
                        OnDidRetrieveWebappInformation)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void ShortcutHelper::WebContentsDestroyed() {
  Destroy();
}

bool ShortcutHelper::RegisterShortcutHelper(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

void ShortcutHelper::AddShortcutInBackground(
    const GURL& url,
    const base::string16& title,
    ShortcutType shortcut_type,
    const favicon_base::FaviconRawBitmapResult& bitmap_result) {
  DCHECK(base::WorkerPool::RunsTasksOnCurrentThread());

  // Grab the average color from the bitmap.
  SkColor color = SK_ColorWHITE;
  SkBitmap favicon_bitmap;
  if (bitmap_result.is_valid()) {
    if (gfx::PNGCodec::Decode(bitmap_result.bitmap_data->front(),
                              bitmap_result.bitmap_data->size(),
                              &favicon_bitmap))
      color = color_utils::CalculateKMeanColorOfBitmap(favicon_bitmap);
  }

  int r_value = SkColorGetR(color);
  int g_value = SkColorGetG(color);
  int b_value = SkColorGetB(color);

  // Send the data to the Java side to create the shortcut.
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> java_url =
      base::android::ConvertUTF8ToJavaString(env, url.spec());
  ScopedJavaLocalRef<jstring> java_title =
      base::android::ConvertUTF16ToJavaString(env, title);
  ScopedJavaLocalRef<jobject> java_bitmap;
  if (favicon_bitmap.getSize())
    java_bitmap = gfx::ConvertToJavaBitmap(&favicon_bitmap);

  Java_ShortcutHelper_addShortcut(env,
                                  base::android::GetApplicationContext(),
                                  java_url.obj(),
                                  java_title.obj(),
                                  java_bitmap.obj(),
                                  r_value,
                                  g_value,
                                  b_value,
                                  shortcut_type != BOOKMARK);

  // Record what type of shortcut was added by the user.
  switch (shortcut_type) {
    case APP_SHORTCUT:
      content::RecordAction(
          base::UserMetricsAction("webapps.AddShortcut.AppShortcut"));
      break;
    case APP_SHORTCUT_APPLE:
      content::RecordAction(
          base::UserMetricsAction("webapps.AddShortcut.AppShortcutApple"));
      break;
    case BOOKMARK:
      content::RecordAction(
          base::UserMetricsAction("webapps.AddShortcut.Bookmark"));
      break;
    default:
      NOTREACHED();
  }
}

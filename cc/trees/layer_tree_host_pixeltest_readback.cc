// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/layers/texture_layer.h"
#include "cc/output/copy_output_request.h"
#include "cc/output/copy_output_result.h"
#include "cc/test/fake_picture_layer.h"
#include "cc/test/fake_picture_layer_impl.h"
#include "cc/test/layer_tree_pixel_test.h"
#include "cc/test/paths.h"
#include "cc/test/solid_color_content_layer_client.h"
#include "cc/trees/layer_tree_impl.h"

#if !defined(OS_ANDROID)

namespace cc {
namespace {

class LayerTreeHostReadbackPixelTest : public LayerTreePixelTest {
 protected:
  LayerTreeHostReadbackPixelTest()
      : readback_type_(READBACK_INVALID),
        insert_copy_request_after_frame_count_(0) {}

  enum ReadbackType {
    READBACK_INVALID,
    READBACK_DEFAULT,
    READBACK_BITMAP,
  };

  void RunReadbackTest(PixelTestType test_type,
                       ReadbackType readback_type,
                       scoped_refptr<Layer> content_root,
                       base::FilePath file_name) {
    readback_type_ = readback_type;
    RunPixelTest(test_type, content_root, file_name);
  }

  void RunReadbackTestWithReadbackTarget(PixelTestType type,
                                         ReadbackType readback_type,
                                         scoped_refptr<Layer> content_root,
                                         Layer* target,
                                         base::FilePath file_name) {
    readback_type_ = readback_type;
    RunPixelTestWithReadbackTarget(type, content_root, target, file_name);
  }

  scoped_ptr<CopyOutputRequest> CreateCopyOutputRequest() override {
    scoped_ptr<CopyOutputRequest> request;

    if (readback_type_ == READBACK_BITMAP) {
      request = CopyOutputRequest::CreateBitmapRequest(
          base::Bind(&LayerTreeHostReadbackPixelTest::ReadbackResultAsBitmap,
                     base::Unretained(this)));
    } else {
      DCHECK_EQ(readback_type_, READBACK_DEFAULT);
      if (test_type_ == PIXEL_TEST_SOFTWARE) {
        request = CopyOutputRequest::CreateRequest(
            base::Bind(&LayerTreeHostReadbackPixelTest::ReadbackResultAsBitmap,
                       base::Unretained(this)));
      } else {
        DCHECK_EQ(test_type_, PIXEL_TEST_GL);
        request = CopyOutputRequest::CreateRequest(
            base::Bind(&LayerTreeHostReadbackPixelTest::ReadbackResultAsTexture,
                       base::Unretained(this)));
      }
    }

    if (!copy_subrect_.IsEmpty())
      request->set_area(copy_subrect_);
    return request.Pass();
  }

  void BeginTest() override {
    if (insert_copy_request_after_frame_count_ == 0) {
      Layer* const target =
          readback_target_ ? readback_target_ : layer_tree_host()->root_layer();
      target->RequestCopyOfOutput(CreateCopyOutputRequest().Pass());
    }
    PostSetNeedsCommitToMainThread();
  }

  void DidCommitAndDrawFrame() override {
    if (insert_copy_request_after_frame_count_ ==
        layer_tree_host()->source_frame_number()) {
      Layer* const target =
          readback_target_ ? readback_target_ : layer_tree_host()->root_layer();
      target->RequestCopyOfOutput(CreateCopyOutputRequest().Pass());
    }
  }

  void ReadbackResultAsBitmap(scoped_ptr<CopyOutputResult> result) {
    EXPECT_TRUE(proxy()->IsMainThread());
    EXPECT_TRUE(result->HasBitmap());
    result_bitmap_ = result->TakeBitmap().Pass();
    EndTest();
  }

  void ReadbackResultAsTexture(scoped_ptr<CopyOutputResult> result) {
    EXPECT_TRUE(proxy()->IsMainThread());
    EXPECT_TRUE(result->HasTexture());

    TextureMailbox texture_mailbox;
    scoped_ptr<SingleReleaseCallback> release_callback;
    result->TakeTexture(&texture_mailbox, &release_callback);
    EXPECT_TRUE(texture_mailbox.IsValid());
    EXPECT_TRUE(texture_mailbox.IsTexture());

    scoped_ptr<SkBitmap> bitmap =
        CopyTextureMailboxToBitmap(result->size(), texture_mailbox);
    release_callback->Run(0, false);

    ReadbackResultAsBitmap(CopyOutputResult::CreateBitmapResult(bitmap.Pass()));
  }

  ReadbackType readback_type_;
  gfx::Rect copy_subrect_;
  int insert_copy_request_after_frame_count_;
};

void IgnoreReadbackResult(scoped_ptr<CopyOutputResult> result) {
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayer_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTest(PIXEL_TEST_SOFTWARE,
                  READBACK_DEFAULT,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayer_Software_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTest(PIXEL_TEST_SOFTWARE,
                  READBACK_BITMAP,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayer_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTest(PIXEL_TEST_GL,
                  READBACK_BITMAP,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayer_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTest(PIXEL_TEST_GL,
                  READBACK_DEFAULT,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayerWithChild_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTest(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayerWithChild_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTest(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackRootLayerWithChild_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTest(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayer_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayer_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayer_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSmallNonRootLayer_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSmallNonRootLayer_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSmallNonRootLayer_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorGREEN);
  background->AddChild(green);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSmallNonRootLayerWithChild_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSmallNonRootLayerWithChild_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSmallNonRootLayerWithChild_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSubtreeSurroundsTargetLayer_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorRED);
  background->AddChild(target);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(-100, -100, 300, 300), SK_ColorGREEN);
  target->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  copy_subrect_ = gfx::Rect(0, 0, 100, 100);
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSubtreeSurroundsLayer_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorRED);
  background->AddChild(target);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(-100, -100, 300, 300), SK_ColorGREEN);
  target->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  copy_subrect_ = gfx::Rect(0, 0, 100, 100);
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSubtreeSurroundsTargetLayer_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(100, 100, 100, 100), SK_ColorRED);
  background->AddChild(target);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(-100, -100, 300, 300), SK_ColorGREEN);
  target->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  copy_subrect_ = gfx::Rect(0, 0, 100, 100);
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSubtreeExtendsBeyondTargetLayer_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(50, 50, 150, 150), SK_ColorRED);
  background->AddChild(target);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(50, 50, 200, 200), SK_ColorGREEN);
  target->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(100, 100, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  copy_subrect_ = gfx::Rect(50, 50, 100, 100);
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSubtreeExtendsBeyondTargetLayer_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(50, 50, 150, 150), SK_ColorRED);
  background->AddChild(target);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(50, 50, 200, 200), SK_ColorGREEN);
  target->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(100, 100, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  copy_subrect_ = gfx::Rect(50, 50, 100, 100);
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackSubtreeExtendsBeyondTargetLayer_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(50, 50, 150, 150), SK_ColorRED);
  background->AddChild(target);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(50, 50, 200, 200), SK_ColorGREEN);
  target->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(100, 100, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  copy_subrect_ = gfx::Rect(50, 50, 100, 100);
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackHiddenSubtree_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorBLACK);

  scoped_refptr<SolidColorLayer> hidden_target =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  hidden_target->SetHideLayerAndSubtree(true);
  background->AddChild(hidden_target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  hidden_target->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      hidden_target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackHiddenSubtree_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorBLACK);

  scoped_refptr<SolidColorLayer> hidden_target =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  hidden_target->SetHideLayerAndSubtree(true);
  background->AddChild(hidden_target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  hidden_target->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      hidden_target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackHiddenSubtree_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorBLACK);

  scoped_refptr<SolidColorLayer> hidden_target =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  hidden_target->SetHideLayerAndSubtree(true);
  background->AddChild(hidden_target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  hidden_target->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      hidden_target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       HiddenSubtreeNotVisibleWhenDrawnForReadback_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorBLACK);

  scoped_refptr<SolidColorLayer> hidden_target =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  hidden_target->SetHideLayerAndSubtree(true);
  background->AddChild(hidden_target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  hidden_target->AddChild(blue);

  hidden_target->RequestCopyOfOutput(CopyOutputRequest::CreateBitmapRequest(
      base::Bind(&IgnoreReadbackResult)));
  RunReadbackTest(PIXEL_TEST_SOFTWARE,
                  READBACK_DEFAULT,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("black.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       HiddenSubtreeNotVisibleWhenDrawnForReadback_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorBLACK);

  scoped_refptr<SolidColorLayer> hidden_target =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  hidden_target->SetHideLayerAndSubtree(true);
  background->AddChild(hidden_target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  hidden_target->AddChild(blue);

  hidden_target->RequestCopyOfOutput(CopyOutputRequest::CreateBitmapRequest(
      base::Bind(&IgnoreReadbackResult)));
  RunReadbackTest(PIXEL_TEST_GL,
                  READBACK_BITMAP,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("black.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       HiddenSubtreeNotVisibleWhenDrawnForReadback_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorBLACK);

  scoped_refptr<SolidColorLayer> hidden_target =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  hidden_target->SetHideLayerAndSubtree(true);
  background->AddChild(hidden_target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  hidden_target->AddChild(blue);

  hidden_target->RequestCopyOfOutput(CopyOutputRequest::CreateBitmapRequest(
      base::Bind(&IgnoreReadbackResult)));
  RunReadbackTest(PIXEL_TEST_GL,
                  READBACK_DEFAULT,
                  background,
                  base::FilePath(FILE_PATH_LITERAL("black.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSubrect_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(100, 100, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  // Grab the middle of the root layer.
  copy_subrect_ = gfx::Rect(50, 50, 100, 100);

  RunReadbackTest(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSubrect_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(100, 100, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  // Grab the middle of the root layer.
  copy_subrect_ = gfx::Rect(50, 50, 100, 100);

  RunReadbackTest(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackSubrect_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(100, 100, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  // Grab the middle of the root layer.
  copy_subrect_ = gfx::Rect(50, 50, 100, 100);

  RunReadbackTest(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayerSubrect_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(25, 25, 150, 150), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(75, 75, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  // Grab the middle of the green layer.
  copy_subrect_ = gfx::Rect(25, 25, 100, 100);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayerSubrect_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(25, 25, 150, 150), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(75, 75, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  // Grab the middle of the green layer.
  copy_subrect_ = gfx::Rect(25, 25, 100, 100);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayerSubrect_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(25, 25, 150, 150), SK_ColorGREEN);
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(75, 75, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  // Grab the middle of the green layer.
  copy_subrect_ = gfx::Rect(25, 25, 100, 100);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackWhenNoDamage_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> parent =
      CreateSolidColorLayer(gfx::Rect(0, 0, 150, 150), SK_ColorRED);
  background->AddChild(parent);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(0, 0, 100, 100), SK_ColorGREEN);
  parent->AddChild(target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  insert_copy_request_after_frame_count_ = 1;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackWhenNoDamage_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> parent =
      CreateSolidColorLayer(gfx::Rect(0, 0, 150, 150), SK_ColorRED);
  background->AddChild(parent);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(0, 0, 100, 100), SK_ColorGREEN);
  parent->AddChild(target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  insert_copy_request_after_frame_count_ = 1;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackWhenNoDamage_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> parent =
      CreateSolidColorLayer(gfx::Rect(0, 0, 150, 150), SK_ColorRED);
  background->AddChild(parent);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(0, 0, 100, 100), SK_ColorGREEN);
  parent->AddChild(target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  insert_copy_request_after_frame_count_ = 1;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackOutsideViewportWhenNoDamage_Software) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> parent =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorRED);
  EXPECT_FALSE(parent->masks_to_bounds());
  background->AddChild(parent);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(250, 250, 100, 100), SK_ColorGREEN);
  parent->AddChild(target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  insert_copy_request_after_frame_count_ = 1;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest,
       ReadbackOutsideViewportWhenNoDamage_GL_Bitmap) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> parent =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorRED);
  EXPECT_FALSE(parent->masks_to_bounds());
  background->AddChild(parent);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(250, 250, 100, 100), SK_ColorGREEN);
  parent->AddChild(target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  insert_copy_request_after_frame_count_ = 1;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_BITMAP,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackOutsideViewportWhenNoDamage_GL) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> parent =
      CreateSolidColorLayer(gfx::Rect(0, 0, 200, 200), SK_ColorRED);
  EXPECT_FALSE(parent->masks_to_bounds());
  background->AddChild(parent);

  scoped_refptr<SolidColorLayer> target =
      CreateSolidColorLayer(gfx::Rect(250, 250, 100, 100), SK_ColorGREEN);
  parent->AddChild(target);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(50, 50, 50, 50), SK_ColorBLUE);
  target->AddChild(blue);

  insert_copy_request_after_frame_count_ = 1;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      target.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

class LayerTreeHostReadbackDeviceScalePixelTest
    : public LayerTreeHostReadbackPixelTest {
 protected:
  LayerTreeHostReadbackDeviceScalePixelTest()
      : device_scale_factor_(1.f),
        white_client_(SK_ColorWHITE),
        green_client_(SK_ColorGREEN),
        blue_client_(SK_ColorBLUE) {}

  void InitializeSettings(LayerTreeSettings* settings) override {
    // Cause the device scale factor to be inherited by contents scales.
    settings->layer_transforms_should_scale_layer_contents = true;
  }

  void SetupTree() override {
    layer_tree_host()->SetDeviceScaleFactor(device_scale_factor_);
    LayerTreePixelTest::SetupTree();
  }

  void DrawLayersOnThread(LayerTreeHostImpl* host_impl) override {
    EXPECT_EQ(device_scale_factor_,
              host_impl->active_tree()->device_scale_factor());
  }

  float device_scale_factor_;
  SolidColorContentLayerClient white_client_;
  SolidColorContentLayerClient green_client_;
  SolidColorContentLayerClient blue_client_;
};

TEST_F(LayerTreeHostReadbackDeviceScalePixelTest, ReadbackSubrect_Software) {
  scoped_refptr<FakePictureLayer> background =
      FakePictureLayer::Create(&white_client_);
  background->SetBounds(gfx::Size(100, 100));
  background->SetIsDrawable(true);

  scoped_refptr<FakePictureLayer> green =
      FakePictureLayer::Create(&green_client_);
  green->SetBounds(gfx::Size(100, 100));
  green->SetIsDrawable(true);
  background->AddChild(green);

  scoped_refptr<FakePictureLayer> blue =
      FakePictureLayer::Create(&blue_client_);
  blue->SetPosition(gfx::Point(50, 50));
  blue->SetBounds(gfx::Size(25, 25));
  blue->SetIsDrawable(true);
  green->AddChild(blue);

  // Grab the middle of the root layer.
  copy_subrect_ = gfx::Rect(25, 25, 50, 50);
  device_scale_factor_ = 2.f;
  RunReadbackTest(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackDeviceScalePixelTest, ReadbackSubrect_GL) {
  scoped_refptr<FakePictureLayer> background =
      FakePictureLayer::Create(&white_client_);
  background->SetBounds(gfx::Size(100, 100));
  background->SetIsDrawable(true);

  scoped_refptr<FakePictureLayer> green =
      FakePictureLayer::Create(&green_client_);
  green->SetBounds(gfx::Size(100, 100));
  green->SetIsDrawable(true);
  background->AddChild(green);

  scoped_refptr<FakePictureLayer> blue =
      FakePictureLayer::Create(&blue_client_);
  blue->SetPosition(gfx::Point(50, 50));
  blue->SetBounds(gfx::Size(25, 25));
  blue->SetIsDrawable(true);
  green->AddChild(blue);

  // Grab the middle of the root layer.
  copy_subrect_ = gfx::Rect(25, 25, 50, 50);
  device_scale_factor_ = 2.f;
  RunReadbackTest(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackDeviceScalePixelTest,
       ReadbackNonRootLayerSubrect_Software) {
  scoped_refptr<FakePictureLayer> background =
      FakePictureLayer::Create(&white_client_);
  background->SetBounds(gfx::Size(100, 100));
  background->SetIsDrawable(true);

  scoped_refptr<FakePictureLayer> green =
      FakePictureLayer::Create(&green_client_);
  green->SetPosition(gfx::Point(10, 20));
  green->SetBounds(gfx::Size(90, 80));
  green->SetIsDrawable(true);
  background->AddChild(green);

  scoped_refptr<FakePictureLayer> blue =
      FakePictureLayer::Create(&blue_client_);
  blue->SetPosition(gfx::Point(50, 50));
  blue->SetBounds(gfx::Size(25, 25));
  blue->SetIsDrawable(true);
  green->AddChild(blue);

  // Grab the green layer's content with blue in the bottom right.
  copy_subrect_ = gfx::Rect(25, 25, 50, 50);
  device_scale_factor_ = 2.f;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackDeviceScalePixelTest,
       ReadbackNonRootLayerSubrect_GL) {
  scoped_refptr<FakePictureLayer> background =
      FakePictureLayer::Create(&white_client_);
  background->SetBounds(gfx::Size(100, 100));
  background->SetIsDrawable(true);

  scoped_refptr<FakePictureLayer> green =
      FakePictureLayer::Create(&green_client_);
  green->SetPosition(gfx::Point(10, 20));
  green->SetBounds(gfx::Size(90, 80));
  green->SetIsDrawable(true);
  background->AddChild(green);

  scoped_refptr<FakePictureLayer> blue =
      FakePictureLayer::Create(&blue_client_);
  blue->SetPosition(gfx::Point(50, 50));
  blue->SetBounds(gfx::Size(25, 25));
  blue->SetIsDrawable(true);
  green->AddChild(blue);

  // Grab the green layer's content with blue in the bottom right.
  copy_subrect_ = gfx::Rect(25, 25, 50, 50);
  device_scale_factor_ = 2.f;
  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_small_with_blue_corner.png")));
}

TEST_F(LayerTreeHostReadbackPixelTest, ReadbackNonRootLayerOutsideViewport) {
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorWHITE);

  scoped_refptr<SolidColorLayer> green =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);
  // Only the top left quarter of the layer is inside the viewport, so the
  // blue layer is entirely outside.
  green->SetPosition(gfx::Point(100, 100));
  background->AddChild(green);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  green->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      PIXEL_TEST_GL,
      READBACK_DEFAULT,
      background,
      green.get(),
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

class LayerTreeHostReadbackNonFirstNonRootRenderPassPixelTest
    : public LayerTreeHostReadbackPixelTest,
      public testing::WithParamInterface<bool> {};

TEST_P(LayerTreeHostReadbackNonFirstNonRootRenderPassPixelTest,
       ReadbackNonRootOrFirstLayer) {
  // This test has 3 render passes with the copy request on the render pass in
  // the middle. Doing a copy request can be destructive of state, so for render
  // passes after the first drawn the code path is different. This verifies the
  // non-first and non-root path. See http://crbug.com/99393 for more info.
  scoped_refptr<SolidColorLayer> background =
      CreateSolidColorLayer(gfx::Rect(200, 200), SK_ColorGREEN);

  scoped_refptr<SolidColorLayer> blue =
      CreateSolidColorLayer(gfx::Rect(150, 150, 50, 50), SK_ColorBLUE);
  blue->RequestCopyOfOutput(CopyOutputRequest::CreateBitmapRequest(
      base::Bind(&IgnoreReadbackResult)));
  background->AddChild(blue);

  RunReadbackTestWithReadbackTarget(
      GetParam() ? PIXEL_TEST_GL : PIXEL_TEST_SOFTWARE,
      READBACK_DEFAULT,
      background,
      background.get(),
      base::FilePath(FILE_PATH_LITERAL("green_with_blue_corner.png")));
}

INSTANTIATE_TEST_CASE_P(
    LayerTreeHostReadbackNonFirstNonRootRenderPassPixelTests,
    LayerTreeHostReadbackNonFirstNonRootRenderPassPixelTest,
    testing::Bool());

}  // namespace
}  // namespace cc

#endif  // OS_ANDROID

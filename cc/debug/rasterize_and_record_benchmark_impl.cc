// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/debug/rasterize_and_record_benchmark_impl.h"

#include <algorithm>
#include <limits>

#include "base/basictypes.h"
#include "base/values.h"
#include "cc/debug/lap_timer.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/picture_layer_impl.h"
#include "cc/resources/tile_task_worker_pool.h"
#include "cc/trees/layer_tree_host_common.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "ui/gfx/geometry/rect.h"

namespace cc {

namespace {

const int kDefaultRasterizeRepeatCount = 100;

class BenchmarkRasterTask : public Task {
 public:
  BenchmarkRasterTask(RasterSource* raster_source,
                      const gfx::Rect& content_rect,
                      float contents_scale,
                      size_t repeat_count)
      : raster_source_(raster_source),
        content_rect_(content_rect),
        contents_scale_(contents_scale),
        repeat_count_(repeat_count),
        is_solid_color_(false),
        best_time_(base::TimeDelta::Max()) {}

  // Overridden from Task:
  void RunOnWorkerThread() override {
    // Parameters for LapTimer.
    const int kTimeLimitMillis = 1;
    const int kWarmupRuns = 0;
    const int kTimeCheckInterval = 1;

    for (size_t i = 0; i < repeat_count_; ++i) {
      // Run for a minimum amount of time to avoid problems with timer
      // quantization when the layer is very small.
      LapTimer timer(kWarmupRuns,
                     base::TimeDelta::FromMilliseconds(kTimeLimitMillis),
                     kTimeCheckInterval);
      do {
        SkBitmap bitmap;
        bitmap.allocPixels(SkImageInfo::MakeN32Premul(content_rect_.width(),
                                                      content_rect_.height()));
        SkCanvas canvas(bitmap);
        RasterSource::SolidColorAnalysis analysis;

        raster_source_->PerformSolidColorAnalysis(content_rect_,
                                                  contents_scale_, &analysis);
        raster_source_->PlaybackToCanvas(&canvas, content_rect_,
                                         contents_scale_);

        is_solid_color_ = analysis.is_solid_color;

        timer.NextLap();
      } while (!timer.HasTimeLimitExpired());
      base::TimeDelta duration =
          base::TimeDelta::FromMillisecondsD(timer.MsPerLap());
      if (duration < best_time_)
        best_time_ = duration;
    }
  }

  bool IsSolidColor() const { return is_solid_color_; }
  base::TimeDelta GetBestTime() const { return best_time_; }

 private:
  ~BenchmarkRasterTask() override {}

  RasterSource* raster_source_;
  gfx::Rect content_rect_;
  float contents_scale_;
  size_t repeat_count_;
  bool is_solid_color_;
  base::TimeDelta best_time_;
};

class FixedInvalidationPictureLayerTilingClient
    : public PictureLayerTilingClient {
 public:
  FixedInvalidationPictureLayerTilingClient(
      PictureLayerTilingClient* base_client,
      const Region invalidation)
      : base_client_(base_client), invalidation_(invalidation) {}

  scoped_refptr<Tile> CreateTile(float contents_scale,
                                 const gfx::Rect& content_rect) override {
    return base_client_->CreateTile(contents_scale, content_rect);
  }

  gfx::Size CalculateTileSize(const gfx::Size& content_bounds) const override {
    return base_client_->CalculateTileSize(content_bounds);
  }

  // This is the only function that returns something different from the base
  // client. Avoids sharing tiles in this area.
  const Region* GetPendingInvalidation() override { return &invalidation_; }

  const PictureLayerTiling* GetPendingOrActiveTwinTiling(
      const PictureLayerTiling* tiling) const override {
    return base_client_->GetPendingOrActiveTwinTiling(tiling);
  }

  PictureLayerTiling* GetRecycledTwinTiling(
      const PictureLayerTiling* tiling) override {
    return base_client_->GetRecycledTwinTiling(tiling);
  }

  TilePriority::PriorityBin GetMaxTilePriorityBin() const override {
    return base_client_->GetMaxTilePriorityBin();
  }

  WhichTree GetTree() const override { return base_client_->GetTree(); }

  bool RequiresHighResToDraw() const override {
    return base_client_->RequiresHighResToDraw();
  }

 private:
  PictureLayerTilingClient* base_client_;
  Region invalidation_;
};

}  // namespace

RasterizeAndRecordBenchmarkImpl::RasterizeAndRecordBenchmarkImpl(
    scoped_refptr<base::MessageLoopProxy> origin_loop,
    base::Value* value,
    const MicroBenchmarkImpl::DoneCallback& callback)
    : MicroBenchmarkImpl(callback, origin_loop),
      rasterize_repeat_count_(kDefaultRasterizeRepeatCount) {
  base::DictionaryValue* settings = nullptr;
  value->GetAsDictionary(&settings);
  if (!settings)
    return;

  if (settings->HasKey("rasterize_repeat_count"))
    settings->GetInteger("rasterize_repeat_count", &rasterize_repeat_count_);
}

RasterizeAndRecordBenchmarkImpl::~RasterizeAndRecordBenchmarkImpl() {}

void RasterizeAndRecordBenchmarkImpl::DidCompleteCommit(
    LayerTreeHostImpl* host) {
  LayerTreeHostCommon::CallFunctionForSubtree(
      host->RootLayer(),
      base::Bind(&RasterizeAndRecordBenchmarkImpl::Run,
                 base::Unretained(this)));

  scoped_ptr<base::DictionaryValue> result(new base::DictionaryValue());
  result->SetDouble("rasterize_time_ms",
                    rasterize_results_.total_best_time.InMillisecondsF());
  result->SetDouble("total_pictures_in_pile_size",
                    rasterize_results_.total_memory_usage);
  result->SetInteger("pixels_rasterized", rasterize_results_.pixels_rasterized);
  result->SetInteger("pixels_rasterized_with_non_solid_color",
                     rasterize_results_.pixels_rasterized_with_non_solid_color);
  result->SetInteger("pixels_rasterized_as_opaque",
                     rasterize_results_.pixels_rasterized_as_opaque);
  result->SetInteger("total_layers", rasterize_results_.total_layers);
  result->SetInteger("total_picture_layers",
                     rasterize_results_.total_picture_layers);
  result->SetInteger("total_picture_layers_with_no_content",
                     rasterize_results_.total_picture_layers_with_no_content);
  result->SetInteger("total_picture_layers_off_screen",
                     rasterize_results_.total_picture_layers_off_screen);

  NotifyDone(result.Pass());
}

void RasterizeAndRecordBenchmarkImpl::Run(LayerImpl* layer) {
  rasterize_results_.total_layers++;
  layer->RunMicroBenchmark(this);
}

void RasterizeAndRecordBenchmarkImpl::RunOnLayer(PictureLayerImpl* layer) {
  rasterize_results_.total_picture_layers++;
  if (!layer->CanHaveTilings()) {
    rasterize_results_.total_picture_layers_with_no_content++;
    return;
  }
  if (layer->visible_content_rect().IsEmpty()) {
    rasterize_results_.total_picture_layers_off_screen++;
    return;
  }

  TaskGraphRunner* task_graph_runner = TileTaskWorkerPool::GetTaskGraphRunner();
  DCHECK(task_graph_runner);

  if (!task_namespace_.IsValid())
    task_namespace_ = task_graph_runner->GetNamespaceToken();

  FixedInvalidationPictureLayerTilingClient client(
      layer, gfx::Rect(layer->content_bounds()));

  // In this benchmark, we will create a local tiling set and measure how long
  // it takes to rasterize content. As such, the actual settings used here don't
  // really matter.
  const LayerTreeSettings& settings = layer->layer_tree_impl()->settings();
  scoped_ptr<PictureLayerTilingSet> tiling_set = PictureLayerTilingSet::Create(
      &client, settings.max_tiles_for_interest_area,
      settings.skewport_target_time_in_seconds,
      settings.skewport_extrapolation_limit_in_content_pixels);

  PictureLayerTiling* tiling = tiling_set->AddTiling(layer->contents_scale_x(),
                                                     layer->GetRasterSource());
  tiling->CreateAllTilesForTesting();
  for (PictureLayerTiling::CoverageIterator it(
           tiling, layer->contents_scale_x(), layer->visible_content_rect());
       it;
       ++it) {
    DCHECK(*it);

    RasterSource* raster_source = (*it)->raster_source();
    gfx::Rect content_rect = (*it)->content_rect();
    float contents_scale = (*it)->contents_scale();

    scoped_refptr<BenchmarkRasterTask> benchmark_raster_task(
        new BenchmarkRasterTask(raster_source,
                                content_rect,
                                contents_scale,
                                rasterize_repeat_count_));

    TaskGraph graph;

    graph.nodes.push_back(
        TaskGraph::Node(benchmark_raster_task.get(),
                        TileTaskWorkerPool::kBenchmarkTaskPriority, 0u));

    task_graph_runner->ScheduleTasks(task_namespace_, &graph);
    task_graph_runner->WaitForTasksToFinishRunning(task_namespace_);

    Task::Vector completed_tasks;
    task_graph_runner->CollectCompletedTasks(task_namespace_, &completed_tasks);
    DCHECK_EQ(1u, completed_tasks.size());
    DCHECK_EQ(completed_tasks[0], benchmark_raster_task);

    int tile_size = content_rect.width() * content_rect.height();
    base::TimeDelta min_time = benchmark_raster_task->GetBestTime();
    bool is_solid_color = benchmark_raster_task->IsSolidColor();

    if (layer->contents_opaque())
      rasterize_results_.pixels_rasterized_as_opaque += tile_size;

    if (!is_solid_color) {
      rasterize_results_.pixels_rasterized_with_non_solid_color += tile_size;
    }

    rasterize_results_.pixels_rasterized += tile_size;
    rasterize_results_.total_best_time += min_time;
  }

  const RasterSource* layer_raster_source = layer->GetRasterSource();
  rasterize_results_.total_memory_usage +=
      layer_raster_source->GetPictureMemoryUsage();
}

RasterizeAndRecordBenchmarkImpl::RasterizeResults::RasterizeResults()
    : pixels_rasterized(0),
      pixels_rasterized_with_non_solid_color(0),
      pixels_rasterized_as_opaque(0),
      total_memory_usage(0),
      total_layers(0),
      total_picture_layers(0),
      total_picture_layers_with_no_content(0),
      total_picture_layers_off_screen(0) {
}

RasterizeAndRecordBenchmarkImpl::RasterizeResults::~RasterizeResults() {}

}  // namespace cc

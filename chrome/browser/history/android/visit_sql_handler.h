// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_HISTORY_ANDROID_VISIT_SQL_HANDLER_H_
#define CHROME_BROWSER_HISTORY_ANDROID_VISIT_SQL_HANDLER_H_

#include "components/history/core/android/sql_handler.h"

namespace base {
class Time;
}

namespace history {

class URLDatabase;
class VisitDatabase;

// This class is the SQLHandler for visits table.
class VisitSQLHandler : public SQLHandler {
 public:
  VisitSQLHandler(URLDatabase* url_db, VisitDatabase* visit_db);
  virtual ~VisitSQLHandler();

  // Overriden from SQLHandler.
  virtual bool Update(const HistoryAndBookmarkRow& row,
                      const TableIDRows& ids_set) override;
  virtual bool Insert(HistoryAndBookmarkRow* row) override;
  virtual bool Delete(const TableIDRows& ids_set) override;

 private:
  // Add a row in visit table with the given |url_id| and |visit_time|.
  bool AddVisit(URLID url_id, const base::Time& visit_time);

  // Add the given |visit_count| rows for |url_id|. The visit time of each row
  // has minium difference and ends with the |last_visit_time|.
  bool AddVisitRows(URLID url_id,
                    int visit_count,
                    const base::Time& last_visit_time);

  // Delete the visits of the given |url_id|.
  bool DeleteVisitsForURL(URLID url_id);

  URLDatabase* url_db_;
  VisitDatabase* visit_db_;

  DISALLOW_COPY_AND_ASSIGN(VisitSQLHandler);
};

}  // namespace history.

#endif  // CHROME_BROWSER_HISTORY_ANDROID_VISIT_SQL_HANDLER_H_

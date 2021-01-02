/*MovieSetImportHandler
 *  Copyright (C) 2020 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MovieSetImportHandler.h"

#include "FileItem.h"
#include "media/import/MediaImport.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"

#include <fmt/ostream.h>

CFileItemPtr CMovieSetImportHandler::FindMatchingLocalItem(
    const CMediaImport& import,
    const CFileItem* item,
    const std::vector<CFileItemPtr>& localItems) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return nullptr;

  const auto& localItem =
      std::find_if(localItems.cbegin(), localItems.cend(), [&item](const CFileItemPtr& localItem) {
        return item->GetVideoInfoTag()->m_strTitle == localItem->GetVideoInfoTag()->m_strTitle;
      });

  if (localItem != localItems.cend())
    return *localItem;

  return nullptr;
}

bool CMovieSetImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  if (m_db.SetDetailsForMovieSet(*(item->GetVideoInfoTag()), item->GetArt(),
                                 item->GetVideoInfoTag()->m_iDbId) <= 0)
  {
    GetLogger()->error("failed to set details for movie set \"{}\" imported from {}",
                       item->GetLabel(), import);
    return false;
  }

  return true;
}

bool CMovieSetImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  return RemoveImportedItem(m_db, import, item);
}

bool CMovieSetImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                           const CMediaImport& import,
                                           std::vector<CFileItemPtr>& items) const
{
  CVideoDbUrl videoUrl;
  videoUrl.FromString("videodb://movies/sets/");
  videoUrl.AddOption("imported", true);
  videoUrl.AddOption("source", import.GetSource().GetIdentifier());
  videoUrl.AddOption("import", import.GetMediaTypesAsString());

  CFileItemList movieSets;
  if (!videodb.GetSetsByWhere(videoUrl.ToString(),
                              CDatabase::Filter(), movieSets, false))
  {
    GetLogger()->error("failed to get previously imported movie sets from {}", import);
    return false;
  }

  items.insert(items.begin(), movieSets.cbegin(), movieSets.cend());

  return true;
}

std::set<Field> CMovieSetImportHandler::IgnoreDifferences() const
{
  return {FieldActor,
          FieldAirDate,
          FieldAlbum,
          FieldArtist,
          FieldCountry,
          FieldDirector,
          FieldEpisodeNumber,
          FieldEpisodeNumberSpecialSort,
          FieldFilename,
          FieldGenre,
          FieldInProgress,
          FieldLastPlayed,
          FieldMPAA,
          FieldOriginalTitle,
          FieldPath,
          FieldPlaycount,
          FieldPlotOutline,
          FieldProductionCode,
          FieldRating,
          FieldSeason,
          FieldSeasonSpecialSort,
          FieldSet,
          FieldSortTitle,
          FieldStudio,
          FieldTag,
          FieldTagline,
          FieldTime,
          FieldTop250,
          FieldTrackNumber,
          FieldTrailer,
          FieldTvShowStatus,
          FieldTvShowTitle,
          FieldUniqueId,
          FieldUserRating,
          FieldWriter};
}

bool CMovieSetImportHandler::AddImportedItem(CVideoDatabase& videodb,
                                             const CMediaImport& import,
                                             CFileItem* item)
{
  if (item == nullptr)
    return false;

  PrepareItem(videodb, import, item);

  item->GetVideoInfoTag()->m_iDbId =
    videodb.SetDetailsForMovieSet(*(item->GetVideoInfoTag()), item->GetArt());
  if (item->GetVideoInfoTag()->m_iDbId <= 0)
  {
    GetLogger()->error("failed to set details for added movie set \"{}\" imported from {}",
      item->GetLabel(), import);
    return false;
  }

  return SetImportForItem(videodb, item, import);
}

bool CMovieSetImportHandler::RemoveImportedItems(CVideoDatabase& videodb, const CMediaImport& import) const
{
  std::vector<CFileItemPtr> items;
  if (!GetLocalItems(videodb, import, items))
    return false;

  for (const auto& item : items)
    RemoveImportedItem(videodb, import, item.get());

  return true;
}

bool CMovieSetImportHandler::RemoveImportedItem(CVideoDatabase& videodb,
                                                const CMediaImport& import,
                                                const CFileItem* item) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return false;

  static const SortDescription sortingCountOnly
  {
    SortByNone,
    SortOrderAscending,
    SortAttributeNone,
    0,
    0
  };

  // get all movies belonging to the current set
  CVideoDbUrl videoUrlAllMoviesInSet;
  videoUrlAllMoviesInSet.FromString("videodb://movies/titles/");
  videoUrlAllMoviesInSet.AddOption("setid", item->GetVideoInfoTag()->m_iDbId);

  // only retrieve the COUNT
  CFileItemList allMoviesInSet;
  if (!videodb.GetMoviesByWhere(videoUrlAllMoviesInSet.ToString(), CDatabase::Filter(),
                                allMoviesInSet, sortingCountOnly))
  {
    GetLogger()->warn("failed to get all movies for set \"{}\"", item->GetLabel(),
      import);
    return false;
  }

  // get only the imported movies belonging to the current set
  CVideoDbUrl videoUrlImportedMoviesInSet;
  videoUrlImportedMoviesInSet.FromString("videodb://movies/titles/");
  videoUrlImportedMoviesInSet.AddOption("imported", true);
  videoUrlImportedMoviesInSet.AddOption("source", import.GetSource().GetIdentifier());
  videoUrlImportedMoviesInSet.AddOption("import", import.GetMediaTypesAsString());
  videoUrlImportedMoviesInSet.AddOption("setid", item->GetVideoInfoTag()->m_iDbId);

  // only retrieve the COUNT
  CFileItemList importedMoviesInSet;
  if (!videodb.GetMoviesByWhere(videoUrlImportedMoviesInSet.ToString(), CDatabase::Filter(),
                                importedMoviesInSet, sortingCountOnly))
  {
    GetLogger()->warn("failed to get all movies for set \"{}\" imported from {}", item->GetLabel(),
      import);
    return false;
  }

  const auto allMoviesInSetCount = GetTotalItemsInDb(allMoviesInSet);
  const auto importedMoviesInSetCount = GetTotalItemsInDb(importedMoviesInSet);

  // check if the set contains any movies not imported from the same import
  if (allMoviesInSetCount >= 0 && importedMoviesInSetCount >= 0 &&
      allMoviesInSetCount == importedMoviesInSetCount)
    videodb.DeleteSet(item->GetVideoInfoTag()->m_iDbId);

  return true;
}

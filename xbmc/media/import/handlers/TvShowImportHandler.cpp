/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "TvShowImportHandler.h"

#include "FileItem.h"
#include "media/import/MediaImport.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"

#include <algorithm>

#include <fmt/ostream.h>

/*!
 * Checks whether two tvshows are the same by comparing them by title and year
 */
static bool IsSameTVShow(const CVideoInfoTag& left, const CVideoInfoTag& right)
{
  return left.m_strTitle == right.m_strTitle && left.GetYear() == right.GetYear();
}

CFileItemPtr CTvShowImportHandler::FindMatchingLocalItem(
    const CMediaImport& import,
    const CFileItem* item,
    const std::vector<CFileItemPtr>& localItems) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return nullptr;

  const auto& localItem =
      std::find_if(localItems.cbegin(), localItems.cend(), [&item](const CFileItemPtr& localItem) {
        return IsSameTVShow(*item->GetVideoInfoTag(), *localItem->GetVideoInfoTag());
      });

  if (localItem != localItems.cend())
    return *localItem;

  return nullptr;
}

bool CTvShowImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  const auto tvshow = item->GetVideoInfoTag();

  std::vector<std::pair<std::string, std::string>> tvshowPaths;
  tvshowPaths.push_back(std::make_pair(item->GetPath(), tvshow->m_basePath));
  std::map<int, std::map<std::string, std::string>> seasonArt;
  if (m_db.SetDetailsForTvShow(tvshowPaths, *tvshow, item->GetArt(), seasonArt, tvshow->m_iDbId) <=
      0)
  {
    GetLogger()->error("failed to set details for tvshow \"{}\" imported from {}", tvshow->m_strTitle,
                       import);
    return false;
  }

  return true;
}

bool CTvShowImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  const auto tvshow = item->GetVideoInfoTag();

  // get the path belonging to the tvshow
  std::pair<int, std::string> tvshowPath;
  if (!m_db.GetPathForImportedItem(tvshow->m_iDbId, GetMediaType(), import, tvshowPath))
  {
    GetLogger()->error("failed to get the path for tvshow \"{}\" imported from {}",
                       tvshow->m_strTitle,import);
    return false;
  }

  // remove the path from the tvshow
  m_db.RemovePathFromTvShow(tvshow->m_iDbId, tvshowPath.first);
  m_db.RemoveImportFromItem(tvshow->m_iDbId, GetMediaType(), import);

  // TODO(Montellese): is this the right place?
  // check if there are any more paths for the tvshow
  std::map<int, std::string> tvshowPaths;
  if (!m_db.GetPathsForTvShow(tvshow->m_iDbId, tvshowPaths))
  {
    GetLogger()->error("failed to get the remaining paths for tvshow \"{}\" imported from {}",
      tvshow->m_strTitle, import);
    return false;
  }

  if (tvshowPaths.empty())
    return RemoveImportedItem(m_db, import, item);

  return true;
}

bool CTvShowImportHandler::CleanupImportedItems(const CMediaImport& import)
{
  if (!m_db.Open())
    return false;

  std::vector<CFileItemPtr> importedTvShows;
  if (!GetLocalItems(m_db, import, importedTvShows))
    return false;

  m_db.BeginTransaction();

  for (const auto& importedTvShow : importedTvShows)
  {
    if (!importedTvShow->HasVideoInfoTag() || importedTvShow->GetVideoInfoTag()->m_iDbId <= 0)
      continue;

    const auto tvshow = importedTvShow->GetVideoInfoTag();

    static const SortDescription sortingCountOnly
    {
      SortByNone,
      SortOrderAscending,
      SortAttributeNone,
      0,
      0
    };

    /* TODO(Montellese)
    // get all episodes of the tvshow
    CVideoDbUrl videoUrlAllEpisodes;
    videoUrlAllEpisodes.FromString(StringUtils::Format("videodb://tvshows/titles/{}/-1/", tvshow->m_iDbId));
    videoUrlAllEpisodes.AddOption("tvshowid", tvshow->m_iDbId);

    // only retrieve the COUNT
    CFileItemList allEpisodes;
    if (!m_db.GetEpisodesByWhere(videoUrlAllEpisodes.ToString(), CDatabase::Filter(), allEpisodes,
                                 true, sortingCountOnly, false))
    {
      GetLogger()->warn("failed to get all episodes for \"{}\" imported from {}",
                        tvshow->m_strShowTitle, import);
      continue;
    }
    */

    // get only imported episodes of the tvshow
    CVideoDbUrl videoUrlImportedEpisodes;
    videoUrlImportedEpisodes.FromString(StringUtils::Format("videodb://tvshows/titles/{}/-1/",
      tvshow->m_iDbId));
    videoUrlImportedEpisodes.AddOption("tvshowid", tvshow->m_iDbId);
    videoUrlImportedEpisodes.AddOption("imported", true);
    videoUrlImportedEpisodes.AddOption("source", import.GetSource().GetIdentifier());
    videoUrlImportedEpisodes.AddOption("import", import.GetMediaTypesAsString());

    // only retrieve the COUNT
    CFileItemList importedEpisodes;
    if (!m_db.GetEpisodesByWhere(videoUrlImportedEpisodes.ToString(), CDatabase::Filter(),
                                 importedEpisodes, true, sortingCountOnly, false))
    {
      GetLogger()->warn("failed to get imported episodes for \"{}\" imported from {}",
                        tvshow->m_strShowTitle, import);
      continue;
    }

    /* TODO(Montellese)
    const auto allEpisodesCount = GetTotalItemsInDb(allEpisodes);
    const auto importedEpisodesCount = GetTotalItemsInDb(importedEpisodes);

    // check if the tvshow contains any episodes not imported from the same import
    if (allEpisodesCount >= 0 && importedEpisodesCount >= 0 &&
        allEpisodesCount == importedEpisodesCount)
      RemoveImportedItem(m_db, import, importedTvShow.get());
    */
    if (GetTotalItemsInDb(importedEpisodes) <= 0)
      RemoveImportedItem(m_db, import, importedTvShow.get());
  }

  m_db.CommitTransaction();

  return true;
}

bool CTvShowImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                         const CMediaImport& import,
                                         std::vector<CFileItemPtr>& items) const
{
  CVideoDbUrl videoUrl;
  videoUrl.FromString("videodb://tvshows/titles/");
  videoUrl.AddOption("imported", true);
  videoUrl.AddOption("source", import.GetSource().GetIdentifier());
  videoUrl.AddOption("import", import.GetMediaTypesAsString());

  CFileItemList tvshows;
  if (!videodb.GetTvShowsByWhere(
          videoUrl.ToString(),
          CDatabase::Filter(), tvshows, SortDescription(),
          import.Settings()->UpdateImportedMediaItems() ? VideoDbDetailsAll : VideoDbDetailsNone))
  {
    GetLogger()->error("failed to get previously imported tvshows from {}", import);
    return false;
  }

  items.insert(items.begin(), tvshows.cbegin(), tvshows.cend());

  return true;
}

std::set<Field> CTvShowImportHandler::IgnoreDifferences() const
{
  return {FieldAlbum,         FieldArtist,
          FieldCountry,       FieldDirector,
          FieldEpisodeNumber, FieldEpisodeNumberSpecialSort,
          FieldFilename,      FieldInProgress,
          FieldLastPlayed,    FieldPlaycount,
          FieldPlotOutline,   FieldProductionCode,
          FieldSeason,        FieldSeasonSpecialSort,
          FieldSet,           FieldTagline,
          FieldTime,          FieldTop250,
          FieldTrackNumber,   FieldTvShowTitle,
          FieldWriter};
}

bool CTvShowImportHandler::AddImportedItem(CVideoDatabase& videodb,
                                           const CMediaImport& import,
                                           CFileItem* item)
{
  if (item == nullptr)
    return false;

  // make sure that the source and import path are set
  PrepareItem(videodb, import, item);

  // and prepare the tvshow paths
  std::vector<std::pair<std::string, std::string>> tvshowPaths;
  tvshowPaths.push_back(std::make_pair(item->GetPath(), item->GetVideoInfoTag()->m_basePath));
  // we don't know the season art yet
  std::map<int, std::map<std::string, std::string>> seasonArt;

  auto info = item->GetVideoInfoTag();

  // check if there already is a local tvshow with the same name
  CFileItemList tvshows;
  videodb.GetTvShowsByName(info->m_strTitle, tvshows);
  bool exists = false;
  if (!tvshows.IsEmpty())
  {
    CFileItemPtr tvshow;
    for (int i = 0; i < tvshows.Size();)
    {
      tvshow = tvshows.Get(i);
      // remove tvshows without a CVideoInfoTag
      if (!tvshow->HasVideoInfoTag())
      {
        tvshows.Remove(i);
        continue;
      }

      CVideoInfoTag* tvshowInfo = tvshow->GetVideoInfoTag();
      if (!videodb.GetTvShowInfo(tvshowInfo->GetPath(), *tvshowInfo, tvshowInfo->m_iDbId,
        tvshow.get()))
      {
        tvshows.Remove(i);
        continue;
      }

      // check if the scraper identifier or the title and year match
      if ((tvshowInfo->HasUniqueID() && tvshowInfo->GetUniqueID() == info->GetUniqueID()) ||
        (tvshowInfo->HasYear() && tvshowInfo->GetYear() == info->GetYear() &&
          tvshowInfo->m_strTitle == info->m_strTitle))
      {
        exists = true;
        break;
      }
      // remove tvshows that don't even match in title
      else if (tvshowInfo->m_strTitle != info->m_strTitle)
      {
        tvshows.Remove(i);
        continue;
      }

      ++i;
    }

    // if there was no exact match and there are still tvshows left that match in title
    // and the new item doesn't have a scraper identifier and no year
    // we take the first match
    if (!exists && !tvshows.IsEmpty() && !info->HasUniqueID() && !info->HasYear())
    {
      tvshow = tvshows.Get(0);
      exists = true;
    }

    // simply add the path of the imported tvshow to the tvshow's paths
    if (exists && tvshow != nullptr)
    {
      info->m_iDbId = videodb.SetDetailsForTvShow(tvshowPaths, *(tvshow->GetVideoInfoTag()),
                                                  tvshow->GetArt(), seasonArt,
                                                  tvshow->GetVideoInfoTag()->m_iDbId);
    }
  }

  // couldn't find a matching local tvshow so add the newly imported one
  if (!exists)
    info->m_iDbId = videodb.SetDetailsForTvShow(tvshowPaths, *info, item->GetArt(), seasonArt);

  // make sure that the tvshow was properly added
  if (info->m_iDbId <= 0)
  {
    GetLogger()->error("failed to set details for added tvshow \"{}\" imported from {}",
      info->m_strTitle, import);
    return false;
  }

  int tvshowPathId = videodb.GetPathId(item->GetPath());
  return SetImportForItem(videodb, item, import, tvshowPathId);
}

bool CTvShowImportHandler::RemoveImportedItems(CVideoDatabase& videodb,
                                               const CMediaImport& import) const
{
  std::vector<CFileItemPtr> items;
  if (!GetLocalItems(videodb, import, items))
    return false;

  for (const auto& item : items)
    RemoveImportedItem(videodb, import, item.get());

  return true;
}

bool CTvShowImportHandler::RemoveImportedItem(CVideoDatabase& videodb,
                                              const CMediaImport& import,
                                              const CFileItem* item) const
{
  // check if the tvshow still has episodes or not
  if (item == nullptr || !item->HasVideoInfoTag())
    return false;

  // if there are other episodes only remove the path and the import link to the tvshow and not the
  // whole tvshow
  if (item->GetVideoInfoTag()->m_iEpisode > 0)
  {
    videodb.RemovePathFromTvShow(item->GetVideoInfoTag()->m_iDbId,
                                 item->GetVideoInfoTag()->GetPath());
    videodb.RemoveImportFromItem(item->GetVideoInfoTag()->m_iDbId, GetMediaType(), import);
  }
  else
    videodb.DeleteTvShow(item->GetVideoInfoTag()->m_iDbId, false, false);

  // either way remove the path
  videodb.DeletePath(-1, item->GetVideoInfoTag()->m_strPath);

  return true;
}

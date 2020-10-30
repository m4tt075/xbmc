/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SeasonImportHandler.h"

#include "FileItem.h"
#include "guilib/LocalizeStrings.h"
#include "media/import/IMediaImportHandlerManager.h"
#include "media/import/MediaImport.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"

#include <algorithm>

#include <fmt/ostream.h>

typedef std::set<CFileItemPtr> TvShowsSet;
typedef std::map<std::string, TvShowsSet> TvShowsMap;

/*!
 * Checks whether two seasons are the same by comparing them by title and year
 */
static bool IsSameSeason(const CVideoInfoTag& left, const CVideoInfoTag& right)
{
  return left.m_strShowTitle == right.m_strShowTitle &&
         (!left.HasYear() || !right.HasYear() || left.GetYear() == right.GetYear()) &&
         left.m_iSeason == right.m_iSeason;
}

std::string CSeasonImportHandler::GetItemLabel(const CFileItem* item) const
{
  if (item != nullptr && item->HasVideoInfoTag() &&
      !item->GetVideoInfoTag()->m_strShowTitle.empty())
  {
    return StringUtils::Format(g_localizeStrings.Get(39565),
                               item->GetVideoInfoTag()->m_strShowTitle,
                               item->GetVideoInfoTag()->m_strTitle);
  }

  return CVideoImportHandler::GetItemLabel(item);
}

CFileItemPtr CSeasonImportHandler::FindMatchingLocalItem(
    const CMediaImport& import,
    const CFileItem* item,
    const std::vector<CFileItemPtr>& localItems) const
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return nullptr;

  const auto& localItem =
      std::find_if(localItems.cbegin(), localItems.cend(), [&item](const CFileItemPtr& localItem) {
        return IsSameSeason(*item->GetVideoInfoTag(), *localItem->GetVideoInfoTag());
      });

  if (localItem != localItems.cend())
    return *localItem;

  return nullptr;
}

bool CSeasonImportHandler::StartSynchronisation(const CMediaImport& import)
{
  if (!CVideoImportHandler::StartSynchronisation(import))
    return false;

  if (m_importHandlerManager == nullptr)
    return false;

  MediaImportHandlerConstPtr tvshowHandlerCreator =
      m_importHandlerManager->GetImportHandler(MediaTypeTvShow);
  if (tvshowHandlerCreator == nullptr)
    return false;

  MediaImportHandlerPtr tvshowHandler(tvshowHandlerCreator->Create());
  if (tvshowHandler == nullptr)
    return false;

  // get all previously imported tvshows
  std::vector<CFileItemPtr> tvshows;
  if (!tvshowHandler->GetLocalItems(import, tvshows))
    return false;

  // create a map of tvshows imported from the same source
  m_tvshows.clear();
  for (const auto tvshow : tvshows)
  {
    if (!tvshow->HasVideoInfoTag() || tvshow->GetVideoInfoTag()->m_strTitle.empty())
      continue;

    auto tvshowsIter = m_tvshows.find(tvshow->GetVideoInfoTag()->m_strTitle);
    if (tvshowsIter == m_tvshows.end())
    {
      TvShowsSet tvshowsSet;
      tvshowsSet.insert(tvshow);
      m_tvshows.insert(make_pair(tvshow->GetVideoInfoTag()->m_strTitle, tvshowsSet));
    }
    else
      tvshowsIter->second.insert(tvshow);
  }

  return true;
}

bool CSeasonImportHandler::AddImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr)
    return false;

  PrepareItem(import, item);

  CVideoInfoTag* season = item->GetVideoInfoTag();

  // try to find an existing tvshow that the season belongs to
  season->m_iIdShow = FindTvShowId(item);

  // if the tvshow doesn't exist, create a very basic version of it with the info we got from the
  // season
  if (season->m_iIdShow <= 0)
  {
    CVideoInfoTag tvshow;
    tvshow.m_basePath = season->m_basePath;
    tvshow.m_cast = season->m_cast;
    tvshow.m_country = season->m_country;
    tvshow.m_director = season->m_director;
    tvshow.m_genre = season->m_genre;
    tvshow.SetYear(season->GetYear());
    tvshow.m_parentPathID = season->m_parentPathID;
    tvshow.m_premiered = season->m_premiered;
    tvshow.m_strMPAARating = season->m_strMPAARating;
    tvshow.m_strPlot = season->m_strPlot;
    tvshow.m_strTitle = tvshow.m_strShowTitle = season->m_strShowTitle;
    tvshow.m_studio = season->m_studio;
    tvshow.m_type = MediaTypeTvShow;
    tvshow.m_writingCredits = season->m_writingCredits;

    // try to find a proper path by going up in the path hierarchy once
    tvshow.m_strPath = URIUtils::GetParentPath(season->GetPath());

    // create an item for the tvshow
    CFileItemPtr tvshowItem(new CFileItem(tvshow));
    tvshowItem->SetPath(tvshow.m_strPath);
    tvshowItem->SetSource(item->GetSource());

    // try to use a tvshow-specific import handler
    bool tvshowImported = false;
    if (m_importHandlerManager != nullptr)
    {
      MediaImportHandlerConstPtr tvshowHandlerCreator =
          m_importHandlerManager->GetImportHandler(MediaTypeTvShow);
      if (tvshowHandlerCreator != nullptr)
      {
        MediaImportHandlerPtr tvshowHandler(tvshowHandlerCreator->Create());
        tvshowImported = tvshowHandler->AddImportedItem(import, tvshowItem.get());
      }
    }

    // fall back to direct database access
    if (!tvshowImported)
    {
      // add the basic tvshow to the database
      std::vector<std::pair<std::string, std::string>> tvshowPaths;
      tvshowPaths.push_back(std::make_pair(tvshow.m_strPath, tvshow.m_basePath));
      tvshow.m_iDbId = tvshow.m_iIdShow =
          m_db.SetDetailsForTvShow(tvshowPaths, tvshow, CGUIListItem::ArtMap(),
                                   std::map<int, std::map<std::string, std::string>>());
    }

    // store the tvshow's database ID in the season
    season->m_iIdShow = tvshow.m_iDbId;

    // add the tvshow to the tvshow map
    auto&& tvshowsIter = m_tvshows.find(tvshow.m_strTitle);
    if (tvshowsIter == m_tvshows.end())
    {
      TvShowsSet tvshowsSet;
      tvshowsSet.insert(tvshowItem);
      m_tvshows.insert(make_pair(tvshow.m_strTitle, tvshowsSet));
    }
    else
      tvshowsIter->second.insert(tvshowItem);
  }

  // check if the season already exists locally
  season->m_iDbId = m_db.GetSeasonId(season->m_iIdShow, season->m_iSeason);

  // no need to add the season again if it already exists locally
  if (season->m_iDbId <= 0)
  {
    season->m_iDbId = m_db.SetDetailsForSeason(*season, item->GetArt(), season->m_iIdShow);
    if (season->m_iDbId <= 0)
    {
      GetLogger()->error("failed to set details for added \"{}\" season {} imported from {}",
                         season->m_strShowTitle, season->m_iSeason, import);
      return false;
    }
  }

  return SetImportForItem(item, import);
}

bool CSeasonImportHandler::UpdateImportedItem(const CMediaImport& import, CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId <= 0)
    return false;

  const auto season = item->GetVideoInfoTag();

  if (m_db.SetDetailsForSeason(*season, item->GetArt(), season->m_iIdShow, season->m_iDbId) <= 0)
  {
    GetLogger()->error("failed to set details for \"{}\" season {} imported from {}",
                       season->m_strShowTitle, season->m_iSeason, import);
    return false;
  }

  return true;
}

bool CSeasonImportHandler::RemoveImportedItem(const CMediaImport& import, const CFileItem* item)
{
  if (item == nullptr || !item->HasVideoInfoTag())
    return false;

  // We don't delete seasons here because part of the season might be local
  // or imported from another source. This will be handled in the cleanup task.
  return true;
}

bool CSeasonImportHandler::CleanupImportedItems(const CMediaImport& import)
{
  if (!m_db.Open())
    return false;

  std::vector<CFileItemPtr> importedSeasons;
  if (!GetLocalItems(m_db, import, importedSeasons))
    return false;

  m_db.BeginTransaction();

  for (const auto& importedSeason : importedSeasons)
  {
    if (!importedSeason->HasVideoInfoTag() || importedSeason->GetVideoInfoTag()->m_iIdShow <= 0)
      continue;

    const auto videoInfoTag = importedSeason->GetVideoInfoTag();

    static const SortDescription sortingCountOnly
    {
      SortByNone,
      SortOrderAscending,
      SortAttributeNone,
      0,
      0
    };

    /* TODO(Montellese)
    // get all episodes of the season of the tvshow
    CVideoDbUrl videoUrlAllEpisodes;
    videoUrlAllEpisodes.FromString(StringUtils::Format("videodb://tvshows/titles/{}/{}/",
      videoInfoTag->m_iIdShow, videoInfoTag->m_iSeason));
    videoUrlAllEpisodes.AddOption("tvshowid", videoInfoTag->m_iIdShow);
    if (importedSeason->GetVideoInfoTag()->m_iSeason >= -1)
      videoUrlAllEpisodes.AddOption("season", videoInfoTag->m_iSeason);

    // only retrieve the COUNT
    CFileItemList allEpisodes;
    if (!m_db.GetEpisodesByWhere(videoUrlAllEpisodes.ToString(), CDatabase::Filter(), allEpisodes,
                                 true, sortingCountOnly, false))
    {
      GetLogger()->warn("failed to get all episodes for \"{}\" season {} imported from {}",
                        videoInfoTag->m_strShowTitle, videoInfoTag->m_iSeason, import);
      continue;
    }
    */

    // get only imported episodes of the season of the tvshow
    CVideoDbUrl videoUrlImportedEpisodes;
    videoUrlImportedEpisodes.FromString(StringUtils::Format("videodb://tvshows/titles/{}/{}/",
      videoInfoTag->m_iIdShow, videoInfoTag->m_iSeason));
    videoUrlImportedEpisodes.AddOption("tvshowid", videoInfoTag->m_iIdShow);
    if (importedSeason->GetVideoInfoTag()->m_iSeason >= -1)
      videoUrlImportedEpisodes.AddOption("season", videoInfoTag->m_iSeason);
    videoUrlImportedEpisodes.AddOption("imported", true);
    videoUrlImportedEpisodes.AddOption("source", import.GetSource().GetIdentifier());
    videoUrlImportedEpisodes.AddOption("import", import.GetMediaTypesAsString());

    // only retrieve the COUNT
    CFileItemList importedEpisodes;
    if (!m_db.GetEpisodesByWhere(videoUrlImportedEpisodes.ToString(), CDatabase::Filter(),
                                 importedEpisodes, true, sortingCountOnly, false))
    {
      GetLogger()->warn("failed to get imported episodes for \"{}\" season {} imported from {}",
                        videoInfoTag->m_strShowTitle, videoInfoTag->m_iSeason, import);
      continue;
    }

    /* TODO(Montellese)
    const auto allEpisodesCount = GetTotalItemsInDb(allEpisodes);
    const auto importedEpisodesCount = GetTotalItemsInDb(importedEpisodes);

    // check if the tvshow contains any episodes not imported from the same import
    if (allEpisodesCount >= 0 && importedEpisodesCount >= 0 &&
        allEpisodesCount == importedEpisodesCount)
      RemoveImportedItem(m_db, import, importedSeason.get());
    */
    if (GetTotalItemsInDb(importedEpisodes) <= 0)
      RemoveImportedItem(m_db, import, importedSeason.get());
  }

  m_db.CommitTransaction();

  return true;
}

bool CSeasonImportHandler::GetLocalItems(CVideoDatabase& videodb,
                                         const CMediaImport& import,
                                         std::vector<CFileItemPtr>& items) const
{
  CVideoDbUrl videoUrl;
  videoUrl.FromString("videodb://tvshows/titles/-1");
  videoUrl.AddOption("showempty", true);
  videoUrl.AddOption("imported", true);
  videoUrl.AddOption("source", import.GetSource().GetIdentifier());
  videoUrl.AddOption("import", import.GetMediaTypesAsString());

  CFileItemList seasons;
  if (!videodb.GetSeasonsByWhere(videoUrl.ToString(), CDatabase::Filter(), seasons, true))
  {
    GetLogger()->error("failed to get previously imported seasons from {}", import);
    return false;
  }

  items.insert(items.begin(), seasons.cbegin(), seasons.cend());

  return true;
}

std::set<Field> CSeasonImportHandler::IgnoreDifferences() const
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
          FieldPlot,
          FieldPlotOutline,
          FieldProductionCode,
          FieldRating,
          FieldSeasonSpecialSort,
          FieldSet,
          FieldSortTitle,
          FieldStudio,
          FieldTag,
          FieldTagline,
          FieldTime,
          FieldTitle,
          FieldTop250,
          FieldTrackNumber,
          FieldTrailer,
          FieldTvShowStatus,
          FieldUniqueId,
          FieldUserRating,
          FieldWriter};
}

bool CSeasonImportHandler::RemoveImportedItems(CVideoDatabase& videodb,
                                               const CMediaImport& import) const
{
  std::vector<CFileItemPtr> items;
  if (!GetLocalItems(videodb, import, items))
    return false;

  for (const auto& item : items)
    RemoveImportedItem(videodb, import, item.get());

  return true;
}

void CSeasonImportHandler::RemoveImportedItem(CVideoDatabase& videodb,
                                              const CMediaImport& import,
                                              const CFileItem* item) const
{
  // check if the season still has episodes or not
  if (item == nullptr || !item->HasVideoInfoTag())
    return;

  // if there are other episodes only remove the import link to the season and not the whole season
  if (item->GetVideoInfoTag()->m_iEpisode > 0)
    videodb.RemoveImportFromItem(item->GetVideoInfoTag()->m_iDbId, GetMediaType(), import);
  else
    videodb.DeleteSeason(item->GetVideoInfoTag()->m_iDbId, false, false);
}

int CSeasonImportHandler::FindTvShowId(const CFileItem* seasonItem)
{
  if (seasonItem == nullptr || !seasonItem->HasVideoInfoTag())
    return -1;

  // no comparison possible without a title
  if (seasonItem->GetVideoInfoTag()->m_strShowTitle.empty())
    return -1;

  // check if there is a tvshow with a matching title
  const auto& tvshowsIter = m_tvshows.find(seasonItem->GetVideoInfoTag()->m_strShowTitle);
  if (tvshowsIter == m_tvshows.end() || tvshowsIter->second.size() <= 0)
    return -1;

  // if there is only one matching tvshow, we can go with that one
  if (tvshowsIter->second.size() == 1)
    return tvshowsIter->second.begin()->get()->GetVideoInfoTag()->m_iDbId;

  // use the path of the episode and tvshow to find the right tvshow
  for (const auto& it : tvshowsIter->second)
  {
    if (URIUtils::PathHasParent(seasonItem->GetVideoInfoTag()->GetPath(),
                                it->GetVideoInfoTag()->GetPath()))
      return it->GetVideoInfoTag()->m_iDbId;
  }

  return -1;
}

#pragma once
/*
 *      Copyright (C) 2016 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <memory>
#include <string>

#include "network/upnp/openHome/rootdevices/ohUPnPRootDevice.h"

class COhUPnPMediaRendererAVTransportService;
class COhUPnPMediaRendererConnectionManagerService;
class COhUPnPRenderingControlService;

class COhUPnPMediaRendererDevice : public COhUPnPRootDevice
{
public:
  COhUPnPMediaRendererDevice(const std::string& uuid,
    const CFileItemElementFactory& fileItemElementFactory,
    COhUPnPTransferManager& transferManager,
    COhUPnPResourceManager& resourceManager);
  virtual ~COhUPnPMediaRendererDevice();

  void UpdateState();

protected:
  virtual void SetupDevice(OpenHome::Net::DvDeviceStdStandard* device) override;

  virtual bool StartServices() override;
  virtual bool StopServices() override;

private:
  // services
  std::unique_ptr<COhUPnPMediaRendererAVTransportService> m_avTransport;
  std::unique_ptr<COhUPnPRenderingControlService> m_renderingControl;
  std::unique_ptr<COhUPnPMediaRendererConnectionManagerService> m_connectionManager;
};

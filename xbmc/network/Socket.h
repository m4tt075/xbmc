/*
 * Socket classes
 *  Copyright (c) 2008 d4rk
 *  Copyright (C) 2008-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <map>
#include <string.h>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "PlatformDefs.h"
#ifdef TARGET_POSIX
typedef int SOCKET;
#endif

namespace SOCKETS
{
  // types of sockets
  enum SocketType
  {
    ST_TCP,
    ST_UDP,
    ST_UNIX
  };

  /**********************************************************************/
  /* IP address abstraction class                                       */
  /**********************************************************************/
  class CAddress
  {
  public:
    union
    {
      sockaddr_in saddr4;
      sockaddr_in6 saddr6;
      sockaddr saddr_generic;
    } saddr;
    socklen_t size;

  public:
    CAddress()
    {
      memset(&saddr, 0, sizeof(saddr));
      saddr.saddr4.sin_family = AF_INET;
      saddr.saddr4.sin_addr.s_addr = htonl(INADDR_ANY);
      size = sizeof(saddr.saddr4);
    }

    explicit CAddress(const char *address)
    {
      SetAddress(address);
    }

    CAddress(const char *address, uint16_t port)
    {
      SetAddress(address);
      SetPort(port);
    }

    void SetAddress(const char *address)
    {
      in6_addr addr6;
      memset(&saddr, 0, sizeof(saddr));
      if (inet_pton(AF_INET6, address, &addr6) == 1)
      {
        saddr.saddr6.sin6_family = AF_INET6;
        saddr.saddr6.sin6_addr = addr6;
        size = sizeof(saddr.saddr6);
      }
      else
      {
        saddr.saddr4.sin_family = AF_INET;
        saddr.saddr4.sin_addr.s_addr = inet_addr(address);
        size = sizeof(saddr.saddr4);
      }
    }

    // returns statically alloced buffer, do not free
    const char *Address()
    {
      if (saddr.saddr_generic.sa_family == AF_INET6)
      {
        static char buf[INET6_ADDRSTRLEN];
        return inet_ntop(AF_INET6, &saddr.saddr6.sin6_addr, buf, size);
      }
      else
        return inet_ntoa(saddr.saddr4.sin_addr);
    }

    unsigned long ULong()
    {
      if (saddr.saddr_generic.sa_family == AF_INET6)
      {
        // IPv4 coercion (see http://home.samfundet.no/~sesse/ipv6-porting.pdf).
        // We hash the entire IPv6 address because XBMC might conceivably need to
        // distinguish between different hosts in the same subnet.
        // This hash function (djbhash) is not too strong, but good enough.
        uint32_t hash = 5381;
        for (int i = 0; i < 16; ++i)
        {
          hash = hash * 33 + saddr.saddr6.sin6_addr.s6_addr[i];
        }
        // Move into 224.0.0.0/3. As a special safeguard, make sure we don't
        // end up with the the special broadcast address 255.255.255.255.
        hash |= 0xe0000000u;
        if (hash == 0xffffffffu)
          hash = 0xfffffffeu;
        return (unsigned long)htonl(hash);
      }
      else
        return (unsigned long)saddr.saddr4.sin_addr.s_addr;
    }

    void SetPort(uint16_t port)
    {
      if (saddr.saddr_generic.sa_family == AF_INET6)
      {
        saddr.saddr6.sin6_port = htons(port);
        size = sizeof(saddr.saddr6);
      }
      else
      {
        saddr.saddr4.sin_port = htons(port);
        size = sizeof(saddr.saddr4);
      }
    }

    uint16_t Port() const
    {
      if (saddr.saddr_generic.sa_family == AF_INET6)
        return ntohs(saddr.saddr6.sin6_port);
      else
        return ntohs(saddr.saddr4.sin_port);
    }
  };

  /**********************************************************************/
  /* Base class for all sockets                                         */
  /**********************************************************************/
  class CBaseSocket
  {
  public:
    CBaseSocket()
      {
        m_Type = ST_TCP;
        m_bReady = false;
        m_bBound = false;
        m_iPort = 0;
      }
    virtual ~CBaseSocket() { Close(); }

    // socket functions
    virtual bool Bind(bool localOnly, int port, int range=0) = 0;
    virtual bool Connect() = 0;
    virtual void Close() {};

    // state functions
    bool Ready() { return m_bReady; }
    bool Bound() { return m_bBound; }
    SocketType Type() { return m_Type; }
    int Port() { return m_iPort; }
    virtual SOCKET Socket() = 0;

  protected:
    virtual void SetBound(bool set=true) { m_bBound = set; }
    virtual void SetReady(bool set=true) { m_bReady = set; }

  protected:
    SocketType m_Type;
    bool m_bReady;
    bool m_bBound;
    int m_iPort;
  };

  /**********************************************************************/
  /* Base class for UDP socket implementations                          */
  /**********************************************************************/
  class CUDPSocket : public CBaseSocket
  {
  public:
    CUDPSocket()
      {
        m_Type = ST_UDP;
        m_ipv4Only = false;
        m_broadcast = false;
      }

    bool IsIp4vOnly() const { return m_ipv4Only; }
    void SetIpv4Only(bool ipv4Only) { m_ipv4Only = ipv4Only; }

    // I/O functions
    virtual int SendTo(const CAddress& addr, const int bufferlength,
                       const void* buffer) = 0;

    // read datagrams, return no. of bytes read or -1 or error
    virtual int Read(CAddress& addr, const int buffersize, void *buffer) = 0;
    virtual bool Broadcast(const CAddress& addr, const int datasize,
                           const void* data) = 0;

  protected:
    bool m_ipv4Only;
  };

  // Implementation specific classes

  /**********************************************************************/
  /* POSIX based UDP socket implementation                              */
  /**********************************************************************/
  class CPosixUDPSocket : public CUDPSocket
  {
  public:
    CPosixUDPSocket()
    {
      m_iSock = INVALID_SOCKET;
      m_ipv6Socket = false;
    }

    bool Bind(bool localOnly, int port, int range=0) override;
    bool Connect() override { return false; }
    bool Listen(int timeout);
    int SendTo(const CAddress& addr, const int datasize, const void* data) override;
    int Read(CAddress& addr, const int buffersize, void *buffer) override;
    bool Broadcast(const CAddress& addr, const int datasize, const void* data) override
    {
      //! @todo implement
      return false;
    }
    SOCKET Socket() override { return m_iSock; }
    void Close() override;

  protected:
    SOCKET m_iSock;
    CAddress m_addr;

  private:
    bool m_ipv6Socket;
  };

  /**********************************************************************/
  /* Create and return platform dependent sockets                       */
  /**********************************************************************/
  class CSocketFactory
  {
  public:
    static CUDPSocket* CreateUDPSocket();
  };

  /**********************************************************************/
  /* Listens on multiple sockets for reads                              */
  /**********************************************************************/

#define LISTENERROR 1
#define LISTENEMPTY 2

  class CSocketListener
  {
  public:
    CSocketListener();
    void AddSocket(CBaseSocket *);
    bool Listen(int timeoutMs); // in ms, -1=>never timeout, 0=>poll
    void Clear();
    CBaseSocket* GetFirstReadySocket();
    CBaseSocket* GetNextReadySocket();

  protected:
    std::vector<CBaseSocket*> m_sockets;
    int m_iReadyCount;
    int m_iMaxSockets;
    int m_iCurrentSocket;
    fd_set m_fdset;
  };

}


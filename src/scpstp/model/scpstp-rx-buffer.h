/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Adrian Sai-wah Tam
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */

#ifndef SCPSTP_RX_BUFFER_H
#define SCPSTP_RX_BUFFER_H


#include <map>
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/sequence-number.h"
#include "ns3/ptr.h"
#include "ns3/tcp-header.h"
#include "ns3/tcp-option-sack.h"
#include "ns3/tcp-rx-buffer.h"

namespace ns3 {

class Packet;


class ScpsTpRxBuffer : public TcpRxBuffer
{ 
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Constructor
   * \param n initial Sequence number to be received
   */
  ScpsTpRxBuffer (uint32_t n = 0);

  virtual ~ScpsTpRxBuffer ();

private:


};




}

















#endif /* SCPSTP_RX_BUFFER_H */
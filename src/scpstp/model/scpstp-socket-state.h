/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2018 Natale Patriciello <natale.patriciello@gmail.com>
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
 */

#ifndef SCPSTP_SOCKET_STATE_H
#define SCPSTP_SOCKET_STATE_H

#include "ns3/object.h"
#include "ns3/data-rate.h"
#include "ns3/traced-value.h"
#include "ns3/sequence-number.h"
#include "scpstp-rx-buffer.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

/**
 * \brief Data structure that records the congestion state of a connection
 *
 * In this data structure, basic information that should be passed between
 * socket and the congestion control algorithm are saved. Through the code,
 * it will be referred as Transmission Control Block (TCB), but there are some
 * differencies. In the RFCs, the TCB contains all the variables that defines
 * a connection, while we preferred to maintain in this class only the values
 * that should be exchanged between socket and other parts, like congestion
 * control algorithms.
 *
 */
class ScpstpSocketState : public TcpSocketState
{
public:
  /**
   * Get the type ID.
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief ScpstpSocketState Constructor
   */
  ScpstpSocketState () : TcpSocketState() { }

  /**
   * \brief Copy constructor.
   * \param other object to copy.
   */
  ScpstpSocketState (const ScpstpSocketState &other);

  Ptr<ScpsTpRxBuffer> m_scpstprxbuffer; //!< The SCPSTP receive buffer

};

} // namespace ns3

#endif /* SCPSTP_SOCKET_STATE_H */
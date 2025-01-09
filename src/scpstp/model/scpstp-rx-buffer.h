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
#include "ns3/scpstp-option-snack.h"

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

  ScpsTpRxBuffer (const TcpRxBuffer &buffer);

  virtual ~ScpsTpRxBuffer ();

  /**
   * \brief Get the snack list
   *
   * The snack list can be empty, and it is updated each time Add or Extract
   * are called through the private method UpdateSackList.
   *
   * \return a list of isolated holes
   */
  ScpsTpOptionSnack::SnackList GetSnackList (void) const;

  /**
   * \brief Get the size of the snack list
   *
   * \return the size of the snack list
   */
  uint32_t GetSnackListSize (void) const;

  /**
   * Insert a packet into the buffer and update the availBytes counter to
   * reflect the number of bytes ready to send to the application. This
   * function handles overlap by triming the head of the inputted packet and
   * removing data from the buffer that overlaps the tail of the inputted
   * packet
   *
   * \param p packet
   * \param tcph packet's TCP header
   * \return True when success, false otherwise.
   */
  //virtual bool Add (Ptr<Packet> p, TcpHeader const& tcph) override;

protected:
  virtual void UpdateSackList (const SequenceNumber32 &head, const SequenceNumber32 &tail) override;

  virtual void ClearSackList (const SequenceNumber32 &seq) override;

private:


  /**
   * \brief Remove old blocks from the sack list
   *
   * Used to remove blocks already delivered to the application.
   *
   * After this call, in the SNACK list there will be only blocks with
   * sequence numbers greater than seq; it is perfectly safe to call this
   * function with an empty snack list.
   *
   * \param seq Last sequence to remove
   */
  void ClearSnackList (const SequenceNumber32 &seq);

  void UpdateSnackList (void);

  ScpsTpOptionSnack::SnackList m_snackList; //!< Snack list

};




}

















#endif /* SCPSTP_RX_BUFFER_H */
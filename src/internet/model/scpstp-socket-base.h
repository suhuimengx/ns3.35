/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 ResiliNets, ITTC, University of Kansas
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
 * Author: Truc Anh N. Nguyen <annguyen@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 */

#ifndef SCPSTPSOCKETBASE_H
#define SCPSTPSOCKETBASE_H

#include "tcp-socket-base.h"

namespace ns3 {

class ScpsTpSocketBase : public TcpSocketBase
{
public:
  enum LossType
  {
    Corruption,  //!< Packet corruption
    Congestion,  //!< Packet loss due to congestion
    Link_Outage //!< Connection interruption
  };

  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  
  /**
   * \brief Get the instance TypeId
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId () const;

  /**
   *  Create an unbound scpstp socket
   */
  ScpsTpSocketBase(void);

  /**
   * \brief Copy constructor
   * \param sock the object to copy
   */
  ScpsTpSocketBase (const ScpsTpSocketBase& sock);

  virtual ~ScpsTpSocketBase(void);

  /**
   * \brief Get the reason for data loss
   * \return the object lossType 
   */
  virtual LossType GetLossType(void) const;
  
  /**
   * \brief Set the reason for data loss
   * \param losstype the reason for data loss
   */
  void SetLossType(LossType losstype);

  

protected:
private:

protected:
  LossType m_lossType;
  


};

} // namespace ns3





#endif //SCPSTPSOCKETBASE_H
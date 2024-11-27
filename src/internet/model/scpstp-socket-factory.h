#ifndef SCPSTP_SOCKET_FACTORY_H
#define SCPSTP_SOCKET_FACTORY_H

#include "ns3/scpstp-socket-base.h"
#include "ns3/socket-factory.h"  
namespace ns3 {

class Socket;

class ScpsTpSocketFactory : public SocketFactory
{
public:
    /**
   * Get the type ID.
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /*
  ScpsTpSocketFactory ();
  virtual ~ScpsTpSocketFactory ();

  virtual Ptr<Socket> CreateSocket (void);
  */
};

}  // namespace ns3

#endif // SCPSTP_SOCKET_FACTORY_H
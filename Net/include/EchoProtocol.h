#ifndef ECHOPROTOCOL_H
#define ECHOPROTOCOL_H

#include "Protocol.h"


namespace Processor
{
    class BoostProcessor;
}
namespace Net
{
    namespace Reactor
    {
        class Reactor;
    }
namespace Protocol
{

    class EchoProtocol:public Net::IProtocol
    {
    public:
        EchoProtocol(
            Reactor::Reactor* theReactor,
            Processor::BoostProcessor* theProcessor);
        ~EchoProtocol();

        void handleInput(Connection::SocketConnectionPtr connection);

        virtual const std::string getAddr();
        virtual int getPort();
        virtual int getRBufferSizePower();
        virtual int getWBufferSizePower();
        virtual int getHeartbeatInterval();
        virtual void handleHeartbeat(Connection::SocketConnectionPtr theConnection); 
    private:
        Reactor::Reactor* reactorM;
        Processor::BoostProcessor* processorM;
    };

}
}

#endif /* ECHOPROTOCOL_H */


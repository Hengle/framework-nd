#include "TelnetProtocol.h"
#include "ConfigCenter.h"
#include "SocketConnection.h"
#include "Log.h"

using namespace Net;
using namespace Net::Protocol;
using namespace Config;

CmdMap TelnetCmdManager::allTopCmdsM;
boost::shared_mutex TelnetCmdManager::topCmdMutexM;
//-----------------------------------------------------------------------------
TelnetCmdManager::TelnetCmdManager(const struct sockaddr_in& thePeerAddr,
				Connection::SocketConnectionPtr theConnection)
	: bufferLenM(0)
    , peerAddrM(thePeerAddr)
	, connectionM(theConnection)
{

}

//-----------------------------------------------------------------------------

TelnetCmdManager::~TelnetCmdManager()
{
}

//-----------------------------------------------------------------------------

bool TelnetCmdManager::validate(const sockaddr_in& thePeerAddr)
{
	return thePeerAddr.sin_family == peerAddrM.sin_family
		&& thePeerAddr.sin_port == peerAddrM.sin_port
		&& thePeerAddr.sin_addr.s_addr == peerAddrM.sin_addr.s_addr;
}

//-----------------------------------------------------------------------------

int TelnetCmdManager::registCmd(
	const std::string& theCmdName,
	ICmdHandler* theHandler)
{
	return 0;
}

//-----------------------------------------------------------------------------

void TelnetCmdManager::send(const char* const theStr, unsigned theLen)
{
    Net::Connection::SocketConnectionPtr connection = connectionM.lock();
    if (connection.get())
    {
        connection->sendn(theStr, theLen);
    }
}

//-----------------------------------------------------------------------------

void TelnetCmdManager::sendPrompt()
{
    Net::Connection::SocketConnectionPtr connection = connectionM.lock();
    if (connection.get())
    {
        const char* const prompt = subCmdStackM.empty() ? "> "
            : subCmdStackM.back()->getPrompt();
        connection->sendn(prompt, strlen(prompt));
    }
}

//-----------------------------------------------------------------------------

int TelnetCmdManager::handleCmd(const unsigned theStart, const unsigned theEnd)
{
    CmdArgsList argsList;
    //trim
    unsigned start = theStart;
    unsigned end = theEnd;
    while (' ' == cmdBufferM[start] ||
           '\t' == cmdBufferM[start] ||
           '\r' == cmdBufferM[start] ||
           '\n' == cmdBufferM[start])
    {
        start++;
        if (start >= theEnd)
        {
            break;
        }
    }
    while (' ' == cmdBufferM[end - 1] ||
           '\t' == cmdBufferM[end - 1] ||
           '\r' == cmdBufferM[end - 1] ||
           '\n' == cmdBufferM[end - 1])
    {
        end--;
        if (end <= start)
        {
            break;
        }
    }

    LOG_DEBUG("handle telnet cmd:" << std::string(cmdBufferM + start, cmdBufferM + end));

    unsigned argsStart = start;
    unsigned i = start;
    for (; i  < end; i++)
    {
        if (' ' == cmdBufferM[i] ||
            '\t' == cmdBufferM[i])
        {
            if (argsStart < i)
                argsList.push_back(std::string(cmdBufferM + argsStart, cmdBufferM + i));
            argsStart = i + 1;
        }

    }
    if (argsStart < i)
    {
        argsList.push_back(std::string(cmdBufferM + argsStart, cmdBufferM + i));
    }
    
    if (argsList.empty())
    {
        sendPrompt();
        return 0;
    }

    // the cmdHandler is not thread-safe, 
    // please make sure it is valid until the man Processor stops
    std::string cmd = argsList.front();
    argsList.pop_front();
    ICmdHandler* cmdHandler = NULL; 
    {
		static CmdMap allTopCmdsM;
        boost::shared_lock<boost::shared_mutex> lock(topCmdMutexM);
        CmdMap::iterator it = allTopCmdsM.find(cmd);
        if (it == allTopCmdsM.end())
        {
            const char* const errstr = "command not found!\n";
            send(errstr, strlen(errstr));
            sendPrompt();
            return 0;
        }
        cmdHandler = it->second;
    }
    cmdHandler->handle(argsList);

    return 0;
}

//-----------------------------------------------------------------------------

int TelnetCmdManager::handleInput()
{
    Net::Connection::SocketConnectionPtr connection = connectionM.lock();
    if (NULL == connection.get())
    {
        return -1;
    }
    while (true)
    {
        unsigned getLen = connection->getInput(cmdBufferM + bufferLenM, sizeof(cmdBufferM) - bufferLenM);
        if (0 == getLen)
        {
            break;
        }
        bufferLenM += getLen;
        unsigned cmdStart = 0;
        for (unsigned i = 0; i < bufferLenM; i++)
        {
            if ('\n' == cmdBufferM[i]) 
            {
                handleCmd(cmdStart, i);
                cmdStart = i + 1;
            }
        }

        //if we have handled a cmd
        if (cmdStart > 0)
        {
            memcpy(cmdBufferM, cmdBufferM + cmdStart, bufferLenM - cmdStart); 
            bufferLenM -= cmdStart;
        }

        //if the cmd is too long
        if (bufferLenM >= sizeof(cmdBufferM))
        {
            //clear buffer
            bufferLenM = 0;
            connection->getInput(NULL, connection->getRBufferSize());
            
            //report error
            const char* const errstr = "cmd is too long\n";
            connection->sendn(errstr, strlen(errstr));
            return 0;
        }

    }
	return 0;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

TelnetProtocol::TelnetProtocol(Processor::BoostProcessor* theProcessor)
	:IProtocol(theProcessor)
{
}

//-----------------------------------------------------------------------------

TelnetProtocol::~TelnetProtocol()
{
}

//-----------------------------------------------------------------------------

void TelnetProtocol::handleInput(Connection::SocketConnectionPtr theConnection)
{
	TelnetCmdManager* cmdManager = NULL;
    int fd = theConnection->getFd();
    {
        boost::upgrade_lock<boost::shared_mutex> lock(manMapMutexM);
        Con2CmdManagerMap::iterator it = con2CmdManagerMapM.find(fd);
        if (it == con2CmdManagerMapM.end())
        {
            cmdManager = new TelnetCmdManager(theConnection->getPeerAddr(), theConnection);		
            boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);
            con2CmdManagerMapM[fd] = cmdManager;
        }
        else if (!it->second->validate(theConnection->getPeerAddr()))
        {
            delete it->second;
            cmdManager = new TelnetCmdManager(theConnection->getPeerAddr(), theConnection);		
            it->second = cmdManager;
        }
        else
        {
            cmdManager = it->second;
        }
    }

    // the same fd's message should be handled in the same thread
    // so no lock is needed in cmdManager
	if (-1 == cmdManager->handleInput())
    {
        theConnection->close();
        boost::unique_lock<boost::shared_mutex> lock(manMapMutexM);
        con2CmdManagerMapM.erase(fd);
        delete cmdManager; 
    }
	return ;
}

//-----------------------------------------------------------------------------

void TelnetProtocol::handleClose(Net::Connection::SocketConnectionPtr theConnection)
{
    LOG_DEBUG("telnet close. fd: " << theConnection->getFd());
    int fd = theConnection->getFd();
    boost::upgrade_lock<boost::shared_mutex> lock(manMapMutexM);
    Con2CmdManagerMap::iterator it = con2CmdManagerMapM.find(fd);
    if (it != con2CmdManagerMapM.end())
    {
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);
        delete it->second;
        con2CmdManagerMapM.erase(it);
    }

}

//-----------------------------------------------------------------------------

void TelnetProtocol::handleConnected(Connection::SocketConnectionPtr theConnection)
{
    LOG_DEBUG("telnet connected. fd: " << theConnection->getFd());
	TelnetCmdManager* cmdManager = NULL;
    int fd = theConnection->getFd();
    {
        boost::upgrade_lock<boost::shared_mutex> lock(manMapMutexM);
        Con2CmdManagerMap::iterator it = con2CmdManagerMapM.find(fd);
        if (it == con2CmdManagerMapM.end())
        {
            cmdManager = new TelnetCmdManager(theConnection->getPeerAddr(), theConnection);		
            boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);
            con2CmdManagerMapM[fd] = cmdManager;
        }
        else
        {
            delete it->second;
            cmdManager = new TelnetCmdManager(theConnection->getPeerAddr(), theConnection);		
            it->second = cmdManager;
        }
    }
    cmdManager->sendPrompt();

}

//-----------------------------------------------------------------------------

const std::string TelnetProtocol::getAddr()
{
    return ConfigCenter::instance()->get("cmd.addr", "127.0.0.1");
}

//-----------------------------------------------------------------------------

int TelnetProtocol::getPort()
{
    return ConfigCenter::instance()->get("cmd.port", 7510);
}

//-----------------------------------------------------------------------------

int TelnetProtocol::getRBufferSizePower()
{
    return 12;
}

//-----------------------------------------------------------------------------

int TelnetProtocol::getWBufferSizePower()
{
    return 13;
}

//-----------------------------------------------------------------------------


/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <wangle/acceptor/ServerSocketConfig.h>
#include <wangle/acceptor/ConnectionCounter.h>
#include <wangle/acceptor/ConnectionManager.h>
#include <wangle/acceptor/LoadShedConfiguration.h>
#include <wangle/acceptor/SecureTransportType.h>
#include <wangle/acceptor/SecurityProtocolContextManager.h>
#include <wangle/acceptor/SSLAcceptorHandshakeHelper.h>
#include <wangle/acceptor/TLSPlaintextPeekingCallback.h>

#include <wangle/ssl/SSLCacheProvider.h>
#include <wangle/acceptor/TransportInfo.h>
#include <wangle/ssl/SSLStats.h>

#include <chrono>
#include <event.h>
#include <folly/ExceptionWrapper.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncUDPServerSocket.h>

namespace wangle {

class AsyncTransport;
class ManagedConnection;
class SecurityProtocolContextManager;
class SSLContextManager;

/**
 * An abstract acceptor for TCP-based network services.
 *
 * There is one acceptor object per thread for each listening socket.  When a
 * new connection arrives on the listening socket, it is accepted by one of the
 * acceptor objects.  From that point on the connection will be processed by
 * that acceptor's thread.
 *
 * The acceptor will call the abstract onNewConnection() method to create
 * a new ManagedConnection object for each accepted socket.  The acceptor
 * also tracks all outstanding connections that it has accepted.
 */
class Acceptor :
  public folly::AsyncServerSocket::AcceptCallback,
  public wangle::ConnectionManager::Callback,
  public folly::AsyncUDPServerSocket::Callback  {
 public:

  enum class State : uint32_t {
    kInit,  // not yet started
    kRunning, // processing requests normally
    kDraining, // processing outstanding conns, but not accepting new ones
    kDone,  // no longer accepting, and all connections finished
  };

  explicit Acceptor(const ServerSocketConfig& accConfig);
  ~Acceptor() override;

  /**
   * Supply an SSL cache provider
   * @note Call this before init()
   */
  virtual void setSSLCacheProvider(
      const std::shared_ptr<SSLCacheProvider>& cacheProvider) {
    cacheProvider_ = cacheProvider;
  }

  /**
   * Initialize the Acceptor to run in the specified EventBase
   * thread, receiving connections from the specified AsyncServerSocket.
   *
   * This method will be called from the AsyncServerSocket's primary thread,
   * not the specified EventBase thread.
   */
  virtual void init(folly::AsyncServerSocket* serverSocket,
                    folly::EventBase* eventBase,
                    SSLStats* stats = nullptr);

  /**
   * Recreates ssl configs, re-reads certs
   */
  virtual void resetSSLContextConfigs();

  /**
   * Dynamically add a new SSLContextConfig
   */
  void addSSLContextConfig(const SSLContextConfig& sslCtxConfig);

  SSLContextManager* getSSLContextManager() const {
    return sslCtxManager_.get();
  }

  /**
   * Sets TLS ticket secrets to use, or updates previously set secrets.
   */
  virtual void setTLSTicketSecrets(
      const std::vector<std::string>& oldSecrets,
      const std::vector<std::string>& currentSecrets,
      const std::vector<std::string>& newSecrets);

  /**
   * Return the number of outstanding connections in this service instance.
   */
  uint32_t getNumConnections() const {
    return downstreamConnectionManager_ ?
      (uint32_t)downstreamConnectionManager_->getNumConnections() : 0;
  }

  /**
   * Access the Acceptor's event base.
   */
  virtual folly::EventBase* getEventBase() const { return base_; }

  /**
   * Access the Acceptor's downstream (client-side) ConnectionManager
   */
  virtual wangle::ConnectionManager* getConnectionManager() {
    return downstreamConnectionManager_.get();
  }

  /**
   * Invoked when a new ManagedConnection is created.
   *
   * This allows the Acceptor to track the outstanding connections,
   * for tracking timeouts and for ensuring that all connections have been
   * drained on shutdown.
   */
  void addConnection(wangle::ManagedConnection* connection);

  /**
   * Get this acceptor's current state.
   */
  State getState() const {
    return state_;
  }

  /**
   * Get the current connection timeout.
   */
  std::chrono::milliseconds getConnTimeout() const;

  /**
   * Returns the name of this VIP.
   *
   * Will return an empty string if no name has been configured.
   */
  const std::string& getName() const {
    return accConfig_.name;
  }

  /**
   * Returns the ssl handshake connection timeout of this VIP
   */
  std::chrono::milliseconds getSSLHandshakeTimeout() const {
    return accConfig_.sslHandshakeTimeout;
  }

  /**
   * Time after drainAllConnections() or acceptStopped() during which
   * new requests on connections owned by the downstream
   * ConnectionManager will be processed normally.
   */
  void setGracefulShutdownTimeout(std::chrono::milliseconds gracefulShutdown) {
    gracefulShutdownTimeout_ = gracefulShutdown;
  }

  std::chrono::milliseconds getGracefulShutdownTimeout() const {
    return gracefulShutdownTimeout_;
  }

  /**
   * Force the acceptor to drop all connections and stop processing.
   *
   * This function may be called from any thread.  The acceptor will not
   * necessarily stop before this function returns: the stop will be scheduled
   * to run in the acceptor's thread.
   */
  virtual void forceStop();

  bool isSSL() const { return accConfig_.isSSL(); }

  const ServerSocketConfig& getConfig() const { return accConfig_; }

  static uint64_t getTotalNumPendingSSLConns() {
    return totalNumPendingSSLConns_.load();
  }

  /**
   * Called right when the TCP connection has been accepted, before processing
   * the first HTTP bytes (HTTP) or the SSL handshake (HTTPS)
   */
  virtual void onDoneAcceptingConnection(
    int fd,
    const folly::SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime
  ) noexcept;

  /**
   * Begins either processing HTTP bytes (HTTP) or the SSL handshake (HTTPS)
   */
  void processEstablishedConnection(
    int fd,
    const folly::SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    TransportInfo& tinfo
  ) noexcept;

  /**
   * Creates and starts the handshake manager.
   */
  virtual void startHandshakeManager(
    folly::AsyncSSLSocket::UniquePtr sslSock,
    Acceptor* acceptor,
    const folly::SocketAddress& clientAddr,
    std::chrono::steady_clock::time_point acceptTime,
    TransportInfo& tinfo) noexcept;

  /**
   * Drains all open connections of their outstanding transactions. When
   * a connection's transaction count reaches zero, the connection closes.
   */
  void drainAllConnections();

  /**
   * Drain defined percentage of connections.
   */
  virtual void drainConnections(double pctToDrain);

  /**
   * Drop all connections.
   *
   * forceStop() schedules dropAllConnections() to be called in the acceptor's
   * thread.
   */
  void dropAllConnections();

  /**
   * Force-drop "pct" (0.0 to 1.0) of remaining client connections,
   * regardless of whether they are busy or idle.
   *
   * Note: unlike dropAllConnections(), this function can be called
   * from any thread.
   */
  virtual void dropConnections(double pctToDrop);

  /**
   * Wrapper for connectionReady() that can be overridden by
   * subclasses to deal with plaintext connections.
   */
   virtual void plaintextConnectionReady(
      folly::AsyncTransportWrapper::UniquePtr sock,
      const folly::SocketAddress& clientAddr,
      const std::string& nextProtocolName,
      SecureTransportType secureTransportType,
      TransportInfo& tinfo);

  /**
   * Process a connection that is to ready to receive L7 traffic.
   * This method is called immediately upon accept for plaintext
   * connections and upon completion of SSL handshaking or resumption
   * for SSL connections.
   */
   void connectionReady(
      folly::AsyncTransportWrapper::UniquePtr sock,
      const folly::SocketAddress& clientAddr,
      const std::string& nextProtocolName,
      SecureTransportType secureTransportType,
      TransportInfo& tinfo);

  /**
   * Wrapper for connectionReady() that decrements the count of
   * pending SSL connections. This should normally not be overridden.
   */
  virtual void sslConnectionReady(folly::AsyncTransportWrapper::UniquePtr sock,
      const folly::SocketAddress& clientAddr,
      const std::string& nextProtocol,
      SecureTransportType secureTransportType,
      TransportInfo& tinfo);

  /**
   * Notification callback for SSL handshake failures.
   */
  virtual void sslConnectionError(const folly::exception_wrapper& ex);

  /**
   * Hook for subclasses to record stats about SSL connection establishment.
   *
   * sock may be nullptr.
   */
  virtual void updateSSLStats(
      const folly::AsyncTransportWrapper* /*sock*/,
      std::chrono::milliseconds /*acceptLatency*/,
      SSLErrorEnum /*error*/) noexcept {}

 protected:

  /**
   * Our event loop.
   *
   * Probably needs to be used to pass to a ManagedConnection
   * implementation. Also visible in case a subclass wishes to do additional
   * things w/ the event loop (e.g. in attach()).
   */
  folly::EventBase* base_{nullptr};

  virtual uint64_t getConnectionCountForLoadShedding(void) const { return 0; }
  virtual uint64_t getActiveConnectionCountForLoadShedding() const { return 0; }
  virtual uint64_t getWorkerMaxConnections() const {
    return connectionCounter_->getMaxConnections();
  }

  /**
   * Hook for subclasses to drop newly accepted connections prior
   * to handshaking.
   */
  virtual bool canAccept(const folly::SocketAddress&);

  /**
   * Invoked when a new connection is created. This is where application starts
   * processing a new downstream connection.
   *
   * NOTE: Application should add the new connection to
   *       downstreamConnectionManager so that it can be garbage collected after
   *       certain period of idleness.
   *
   * @param sock                the socket connected to the client
   * @param address             the address of the client
   * @param nextProtocolName    the name of the L6 or L7 protocol to be
   *                              spoken on the connection, if known (e.g.,
   *                              from TLS NPN during secure connection setup),
   *                              or an empty string if unknown
   * @param secureTransportType the name of the secure transport type that was
   *                            requested by the client.
   */
  virtual void onNewConnection(
      folly::AsyncTransportWrapper::UniquePtr /*sock*/,
      const folly::SocketAddress* /*address*/,
      const std::string& /*nextProtocolName*/,
      SecureTransportType /*secureTransportType*/,
      const TransportInfo& /*tinfo*/) {}

  void onListenStarted() noexcept override {}
  void onListenStopped() noexcept override {}
  void onDataAvailable(
      std::shared_ptr<folly::AsyncUDPSocket> /*socket*/,
      const folly::SocketAddress&,
      std::unique_ptr<folly::IOBuf>,
      bool) noexcept override {}

  virtual folly::AsyncSocket::UniquePtr makeNewAsyncSocket(
      folly::EventBase* base,
      int fd) {
    return folly::AsyncSocket::UniquePtr(
        new folly::AsyncSocket(base, fd));
  }

  virtual folly::AsyncSSLSocket::UniquePtr makeNewAsyncSSLSocket(
    const std::shared_ptr<folly::SSLContext>& ctx, folly::EventBase* base, int fd) {
    return folly::AsyncSSLSocket::UniquePtr(
        new folly::AsyncSSLSocket(
          ctx,
          base,
          fd,
          true, /* set server */
          true /* defer the security negotiation until sslAccept */));
  }

 protected:

  /**
   * onConnectionsDrained() will be called once all connections have been
   * drained while the acceptor is stopping.
   *
   * Subclasses can override this method to perform any subclass-specific
   * cleanup.
   */
  virtual void onConnectionsDrained() {}

  // AsyncServerSocket::AcceptCallback methods
  void connectionAccepted(
      int fd,
      const folly::SocketAddress& clientAddr) noexcept override;
  void acceptError(const std::exception& ex) noexcept override;
  void acceptStopped() noexcept override;

  // ConnectionManager::Callback methods
  void onEmpty(const wangle::ConnectionManager& cm) override;
  void onConnectionAdded(const wangle::ConnectionManager& /*cm*/) override {}
  void onConnectionRemoved(const wangle::ConnectionManager& /*cm*/) override {}

  const LoadShedConfiguration& getLoadShedConfiguration() const {
    return loadShedConfig_;
  }

 protected:
  const ServerSocketConfig accConfig_;
  void setLoadShedConfig(const LoadShedConfiguration& from,
                         IConnectionCounter* counter);

  // Helper function to initialize downstreamConnectionManager_
  virtual void initDownstreamConnectionManager(folly::EventBase* eventBase);


  /**
   * Socket options to apply to the client socket
   */
  folly::AsyncSocket::OptionMap socketOptions_;

  std::unique_ptr<SSLContextManager> sslCtxManager_;

  /**
   * Stores peekers for different security protocols.
   */
  SecurityProtocolContextManager securityProtocolCtxManager_;

  TLSPlaintextPeekingCallback tlsPlaintextPeekingCallback_;
  DefaultToSSLPeekingCallback defaultPeekingCallback_;

  wangle::ConnectionManager::UniquePtr downstreamConnectionManager_;

  std::shared_ptr<SSLCacheProvider> cacheProvider_;

 private:

  // Forbidden copy constructor and assignment opererator
  Acceptor(Acceptor const &) = delete;
  Acceptor& operator=(Acceptor const &) = delete;

  void checkDrained();

  State state_{State::kInit};
  uint64_t numPendingSSLConns_{0};

  static std::atomic<uint64_t> totalNumPendingSSLConns_;

  bool forceShutdownInProgress_{false};
  LoadShedConfiguration loadShedConfig_;
  IConnectionCounter* connectionCounter_{nullptr};
  std::chrono::milliseconds gracefulShutdownTimeout_{5000};
};

class AcceptorFactory {
 public:
  virtual std::shared_ptr<Acceptor> newAcceptor(folly::EventBase*) = 0;
  virtual ~AcceptorFactory() = default;
};

} // namespace

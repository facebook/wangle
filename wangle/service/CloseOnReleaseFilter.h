#pragma once

#include <wangle/service/Service.h>

namespace folly { namespace wangle {

/**
 * A service that rejects all requests after its 'close' method has
 * been invoked.
 */
template <typename Req, typename Resp>
class CloseOnReleaseFilter : public ServiceFilter<Req, Resp> {
 public:
  explicit CloseOnReleaseFilter(std::shared_ptr<Service<Req, Resp>> service)
      : ServiceFilter<Req, Resp>(service) {}

  Future<Resp> operator()(Req req) override {
    if (!released ){
      return (*this->service_)(std::move(req));
    } else {
      return makeFuture<Resp>(
        make_exception_wrapper<std::runtime_error>("Service Closed"));
    }
  }

  Future<void> close() override {
    if (!released.exchange(true)) {
      return this->service_->close();
    } else {
      return makeFuture();
    }
  }
 private:
  std::atomic<bool> released{false};
};

}} // namespace

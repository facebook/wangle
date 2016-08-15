#pragma once

/**
 * An enum representing different kinds
 * of secure transports we can negotiate.
 */
enum class SecureTransportType {
  NONE, // Transport is not secure.
  TLS,  // Transport is based on TLS
  ZERO, // Transport is based on zero protocol
};

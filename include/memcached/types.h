#pragma once

#include <boost/optional/optional.hpp>
#include <memcached/dockey.h>
#include <sys/types.h>
#include <chrono>
#include <cstdint>
#include <iosfwd>

#ifdef WIN32
// Need DWORD and ssize_t (used to be defined in platform/platform.h
#ifndef WIN32_LEAN_AND_MEAN
#define DO_UNDEF_WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#ifdef DO_UNDEF_WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
typedef long ssize_t;
#else
#include <sys/uio.h>
#endif

#include "vbucket.h"

/**
 * Time relative to server start. Smaller than time_t on 64-bit systems.
 */
typedef uint32_t rel_time_t;

/**
 * Engine storage operations.
 */
typedef enum {
    OPERATION_ADD = 1, /**< Store with add semantics */
    OPERATION_SET = 2, /**< Store with set semantics */
    OPERATION_REPLACE = 3, /**< Store with replace semantics */
    OPERATION_CAS = 6 /**< Store with set semantics. */
} ENGINE_STORE_OPERATION;

typedef enum {
    CONN_PRIORITY_HIGH,
    CONN_PRIORITY_MED,
    CONN_PRIORITY_LOW
} CONN_PRIORITY;

/**
 * Data common to any item stored in memcached.
 */
typedef void item;

/**
 * Constant value representing the masked CAS we return if an item is under lock
 */
static constexpr uint64_t LOCKED_CAS = std::numeric_limits<uint64_t>::max();

/**
 * The legal state a document may be in (from the cores perspective)
 */
enum class DocumentState : uint8_t {
    /**
     * The document is deleted from the users perspective, and trying
     * to fetch the document will return KEY_NOT_FOUND unless one
     * asks specifically for deleted documents. The Deleted documents
     * will not hang around forever and may be reaped by the purger
     * at any time (from the core's perspective. That's an internal
     * detail within the underlying engine).
     */
    Deleted = 0x0F,

    /**
     * The document is alive and all operations should work as expected.
     */
    Alive = 0xF0,
};

std::string to_string(DocumentState& ds);
std::ostream& operator<<(std::ostream& os, DocumentState& ds);

/**
 * The DocumentStateFilter is an enum which allows you to specify the state(s)
 * a document may have.
 */
enum class DocStateFilter : uint8_t {
    /// Only alive documents match this filter
    Alive = uint8_t(DocumentState::Alive),
    /// Only deleted documents match this filter
    Deleted = uint8_t(DocumentState::Deleted),
    /// The document may be alive or deleted.
    AliveOrDeleted = uint8_t(uint8_t(Alive) | uint8_t(Deleted))
};

struct item_info {
    item_info()
        : cas(0),
          vbucket_uuid(0),
          seqno(0),
          exptime(0),
          nbytes(0),
          flags(0),
          datatype(0),
          document_state(DocumentState::Deleted),
          value{},
          cas_is_hlc(false) {
    }
    uint64_t cas;
    uint64_t vbucket_uuid;
    uint64_t seqno;
    time_t exptime; /**< When the item will expire (absolute time) */
    uint32_t nbytes; /**< The total size of the data (in bytes) */
    uint32_t flags; /**< Flags associated with the item (in network byte order)*/
    uint8_t datatype;

    /**
     * The current state of the document (Deleted or Alive).
     */
    DocumentState document_state;

    /**
     * If the xattr bit is set in datatype the first uint32_t contains
     * the size of the extended attributes which follows next, and
     * finally the actual document payload.
     */
    struct iovec value[1];

    /**
     * True if the CAS is a HLC timestamp
     */
    bool cas_is_hlc;

    /**
     * Item's DocKey
     */
    DocKey key{nullptr, 0, DocKeyEncodesCollectionId::No};
};

/* Forward declaration of the server handle -- to be filled in later */
typedef struct server_handle_v1_t SERVER_HANDLE_V1;

/* Information to uniquely identify (and order) a mutation. */
typedef struct {
    uint64_t vbucket_uuid; /** vBucket UUID for this mutation. */
    uint64_t seqno; /** sequence number of the mutation. */
} mutation_descr_t;

/* Value used to distinguish one bucket from another */
typedef uint32_t bucket_id_t;

namespace cb {
struct vbucket_info {
    /// has the vbucket has had xattr documents written to it
    bool mayContainXattrs;
};

using ExpiryLimit = boost::optional<std::chrono::seconds>;

static const ExpiryLimit NoExpiryLimit{};
}

/**
 * DeleteSource denotes the source of an item's deletion;
 * either explicitly or TTL (expired).
 */
enum class DeleteSource : uint8_t { Explicit = 0, TTL = 1 };

/**
 * Convert deletionSource to string for visual output
 */
std::string to_string(DeleteSource deleteSource);

/**
 * The committed state of the Item.
 *
 * Consists of three states: CommittedViaMutation, CommittedViaPrepare and
 * Pending. The first two are generally considered as the same 'Committed' state
 * by external observers, but internally we need to differentiate between
 * them to write to disk / send over DCP hence having two different states.
 *
 * Used in a bitfield in StoredValue hence explicit values for enums required.
 */
enum class CommittedState : char {
    /// Item is committed, by virtue of being a plain mutation - i.e. not added
    /// via a SyncWrite.
    CommittedViaMutation = 0,
    /// Item is committed by virtue of previously being a pending SyncWrite
    /// which was committed.
    CommittedViaPrepare = 1,
    /// Item is pending (is not yet committed) and hence not visible to
    /// external clients yet.
    Pending = 2,
};

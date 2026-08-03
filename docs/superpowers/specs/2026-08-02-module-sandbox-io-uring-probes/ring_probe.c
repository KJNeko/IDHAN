// Probes the three load-bearing claims of the per-call restricted-ring design:
//   1. a restricted ring leaks its registered file's path via /proc/self/fdinfo
//   2. the restrictions actually deny non-READ opcodes and non-FIXED_FILE reads
//   3. registering the ring fd lets us close the real fd, removing the leak
//
//   cc -O1 -o ring_probe ring_probe.c -luring && ./ring_probe [path]

#define _GNU_SOURCE
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void dump_fdinfo( const int ring_fd, const char* label )
{
	char proc_path[ 64 ];
	snprintf( proc_path, sizeof( proc_path ), "/proc/self/fdinfo/%d", ring_fd );

	FILE* f = fopen( proc_path, "r" );
	if ( !f )
	{
		printf( "  [%s] fdinfo unreadable: %m\n", label );
		return;
	}

	char line[ 512 ];
	int printed = 0;
	while ( fgets( line, sizeof( line ), f ) )
	{
		// Only the registered-file section matters; the rest is ring bookkeeping.
		if ( strstr( line, "UserFiles" ) || line[ 0 ] == ' ' || strchr( line, '/' ) )
		{
			printf( "  [%s] %s", label, line );
			printed = 1;
		}
	}
	if ( !printed ) printf( "  [%s] no registered-file lines in fdinfo\n", label );
	fclose( f );
}

static int build_ring( struct io_uring* ring, const int file_fd, const int allow_ring_fds )
{
	struct io_uring_params params;
	memset( &params, 0, sizeof( params ) );
	params.flags = IORING_SETUP_R_DISABLED;

	int rc = io_uring_queue_init_params( 8, ring, &params );
	if ( rc < 0 )
	{
		printf( "  io_uring_queue_init_params: %s\n", strerror( -rc ) );
		return rc;
	}

	rc = io_uring_register_files( ring, &file_fd, 1 );
	if ( rc < 0 )
	{
		printf( "  io_uring_register_files: %s\n", strerror( -rc ) );
		return rc;
	}

	struct io_uring_restriction res[ 4 ];
	memset( res, 0, sizeof( res ) );
	unsigned n = 0;

	res[ n ].opcode = IORING_RESTRICTION_SQE_OP;
	res[ n++ ].sqe_op = IORING_OP_READ;

	res[ n ].opcode = IORING_RESTRICTION_SQE_FLAGS_REQUIRED;
	res[ n++ ].sqe_flags = IOSQE_FIXED_FILE;

	if ( allow_ring_fds )
	{
		res[ n ].opcode = IORING_RESTRICTION_REGISTER_OP;
		res[ n++ ].register_op = IORING_REGISTER_RING_FDS;
	}

	rc = io_uring_register_restrictions( ring, res, n );
	if ( rc < 0 )
	{
		printf( "  io_uring_register_restrictions: %s\n", strerror( -rc ) );
		return rc;
	}

	rc = io_uring_enable_rings( ring );
	if ( rc < 0 )
	{
		printf( "  io_uring_enable_rings: %s\n", strerror( -rc ) );
		return rc;
	}

	return 0;
}

// Reads 16 bytes from registered slot 0. fixed=0 deliberately omits IOSQE_FIXED_FILE.
static int try_read( struct io_uring* ring, const int fixed )
{
	char buf[ 16 ];
	struct io_uring_sqe* sqe = io_uring_get_sqe( ring );
	io_uring_prep_read( sqe, 0, buf, sizeof( buf ), 0 );
	if ( fixed ) sqe->flags |= IOSQE_FIXED_FILE;

	const int submitted = io_uring_submit( ring );
	if ( submitted < 0 ) return submitted;

	struct io_uring_cqe* cqe;
	const int rc = io_uring_wait_cqe( ring, &cqe );
	if ( rc < 0 ) return rc;

	const int result = cqe->res;
	io_uring_cqe_seen( ring, cqe );
	return result;
}

static int try_openat( struct io_uring* ring )
{
	struct io_uring_sqe* sqe = io_uring_get_sqe( ring );
	io_uring_prep_openat( sqe, AT_FDCWD, "/etc/passwd", O_RDONLY, 0 );
	sqe->flags |= IOSQE_FIXED_FILE;

	const int submitted = io_uring_submit( ring );
	if ( submitted < 0 ) return submitted;

	struct io_uring_cqe* cqe;
	const int rc = io_uring_wait_cqe( ring, &cqe );
	if ( rc < 0 ) return rc;

	const int result = cqe->res;
	io_uring_cqe_seen( ring, cqe );
	return result;
}

int main( const int argc, char** argv )
{
	const char* path = argc > 1 ? argv[ 1 ] : "/etc/hostname";

	const int file_fd = open( path, O_RDONLY );
	if ( file_fd < 0 )
	{
		printf( "open(%s): %m\n", path );
		return 1;
	}

	printf( "target file: %s\n\n", path );

	// --- 1. does the sealed ring leak the path? -------------------------------
	printf( "1. sealed ring, fdinfo:\n" );
	struct io_uring ring;
	if ( build_ring( &ring, file_fd, 0 ) < 0 ) return 1;
	dump_fdinfo( ring.ring_fd, "leak" );

	// --- 2. do the restrictions bite? -----------------------------------------
	printf( "\n2. restrictions:\n" );
	const int fixed_read = try_read( &ring, 1 );
	printf( "  READ  + IOSQE_FIXED_FILE -> %s\n", fixed_read < 0 ? strerror( -fixed_read ) : "bytes read (expected)" );

	const int bare_read = try_read( &ring, 0 );
	printf( "  READ  without FIXED_FILE -> %s\n", bare_read < 0 ? strerror( -bare_read ) : "SUCCEEDED (unexpected!)" );

	const int opened = try_openat( &ring );
	printf( "  OPENAT                   -> %s\n", opened < 0 ? strerror( -opened ) : "SUCCEEDED (unexpected!)" );

	const int reg = io_uring_register_ring_fd( &ring );
	printf( "  REGISTER_RING_FDS        -> %s\n", reg < 0 ? strerror( -reg ) : "allowed" );
	io_uring_queue_exit( &ring );

	// --- 3. can we register the ring fd and close it, removing the leak? ------
	printf( "\n3. ring with REGISTER_RING_FDS permitted:\n" );
	struct io_uring ring2;
	if ( build_ring( &ring2, file_fd, 1 ) < 0 ) return 1;

	const int reg2 = io_uring_register_ring_fd( &ring2 );
	if ( reg2 < 0 )
	{
		printf( "  io_uring_register_ring_fd: %s\n", strerror( -reg2 ) );
		return 1;
	}
	printf( "  registered ring fd, index %d\n", reg2 );

	const int old_fd = ring2.ring_fd;
	close( old_fd );
	printf( "  closed real ring fd %d\n", old_fd );
	dump_fdinfo( old_fd, "after close" );

	const int post = try_read( &ring2, 1 );
	printf( "  READ after closing the fd -> %s\n", post < 0 ? strerror( -post ) : "bytes read (still works)" );

	return 0;
}

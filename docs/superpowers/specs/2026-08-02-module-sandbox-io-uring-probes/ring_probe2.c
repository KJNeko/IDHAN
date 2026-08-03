// Does a ring with only SQE restrictions still permit register ops?
// And does naming one REGISTER_OP restriction close the rest?
//
//   cc -O1 -o ring_probe2 ring_probe2.c -luring && ./ring_probe2

#define _GNU_SOURCE
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int build( struct io_uring* ring, const int file_fd, const int restrict_register )
{
	struct io_uring_params params;
	memset( &params, 0, sizeof( params ) );
	params.flags = IORING_SETUP_R_DISABLED;

	int rc = io_uring_queue_init_params( 8, ring, &params );
	if ( rc < 0 ) return rc;

	rc = io_uring_register_files( ring, &file_fd, 1 );
	if ( rc < 0 ) return rc;

	struct io_uring_restriction res[ 3 ];
	memset( res, 0, sizeof( res ) );
	unsigned n = 0;

	res[ n ].opcode = IORING_RESTRICTION_SQE_OP;
	res[ n++ ].sqe_op = IORING_OP_READ;

	res[ n ].opcode = IORING_RESTRICTION_SQE_FLAGS_REQUIRED;
	res[ n++ ].sqe_flags = IOSQE_FIXED_FILE;

	if ( restrict_register )
	{
		res[ n ].opcode = IORING_RESTRICTION_REGISTER_OP;
		res[ n++ ].register_op = IORING_REGISTER_RING_FDS;
	}

	rc = io_uring_register_restrictions( ring, res, n );
	if ( rc < 0 ) return rc;

	return io_uring_enable_rings( ring );
}

// The attacker's move: swap the registered slot for a descriptor of our choosing.
static int try_files_update( struct io_uring* ring, const int replacement )
{
	return io_uring_register_files_update( ring, 0, &replacement, 1 );
}

static void probe( const int restrict_register, const int file_fd, const int intruder )
{
	printf( "\n=== REGISTER_OP restriction %s ===\n", restrict_register ? "SET (RING_FDS only)" : "NOT SET" );

	struct io_uring ring;
	const int rc = build( &ring, file_fd, restrict_register );
	if ( rc < 0 )
	{
		printf( "  setup failed: %s\n", strerror( -rc ) );
		return;
	}

	const int upd = try_files_update( &ring, intruder );
	printf( "  FILES_UPDATE (swap slot 0) -> %s\n", upd < 0 ? strerror( -upd ) : "SUCCEEDED -- slot swapped!" );

	const int buffers_rc = io_uring_register_buffers( &ring, &( struct iovec ) { NULL, 0 }, 1 );
	printf( "  REGISTER_BUFFERS           -> %s\n", buffers_rc < 0 ? strerror( -buffers_rc ) : "allowed" );

	const int reg = io_uring_register_ring_fd( &ring );
	printf( "  REGISTER_RING_FDS          -> %s\n", reg < 0 ? strerror( -reg ) : "allowed" );

	// If the swap worked, whose bytes do we get now?
	char buf[ 32 ];
	memset( buf, 0, sizeof( buf ) );
	struct io_uring_sqe* sqe = io_uring_get_sqe( &ring );
	io_uring_prep_read( sqe, 0, buf, sizeof( buf ) - 1, 0 );
	sqe->flags |= IOSQE_FIXED_FILE;
	io_uring_submit( &ring );

	struct io_uring_cqe* cqe;
	if ( io_uring_wait_cqe( &ring, &cqe ) == 0 )
	{
		if ( cqe->res > 0 )
		{
			for ( int i = 0; buf[ i ]; ++i )
				if ( buf[ i ] == '\n' ) buf[ i ] = ' ';
			printf( "  slot 0 now reads: \"%s\"\n", buf );
		}
		else
			printf( "  slot 0 read -> %s\n", strerror( -cqe->res ) );
		io_uring_cqe_seen( &ring, cqe );
	}

	io_uring_queue_exit( &ring );
}

int main( void )
{
	const int file_fd = open( "/etc/hostname", O_RDONLY );
	// Stands in for any descriptor a compromised worker already holds.
	const int intruder = open( "/etc/os-release", O_RDONLY );

	if ( file_fd < 0 || intruder < 0 )
	{
		printf( "open: %m\n" );
		return 1;
	}

	printf( "slot 0 starts as /etc/hostname; intruder fd is /etc/os-release\n" );

	probe( 0, file_fd, intruder );
	probe( 1, file_fd, intruder );

	return 0;
}

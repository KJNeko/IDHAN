// Can a restricted ring be enabled with an empty (sparse) file table and have
// slots filled in later? The design registers N empty slots before fork.
#define _GNU_SOURCE
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main( void )
{
	struct io_uring_params p;
	memset( &p, 0, sizeof( p ) );
	p.flags = IORING_SETUP_R_DISABLED;

	struct io_uring ring;
	if ( io_uring_queue_init_params( 8, &ring, &p ) < 0 ) return 1;

	int rc = io_uring_register_files_sparse( &ring, 4 );
	printf( "register 4 sparse slots -> %s\n", rc < 0 ? strerror( -rc ) : "ok" );

	struct io_uring_restriction res[ 3 ];
	memset( res, 0, sizeof( res ) );
	res[ 0 ].opcode = IORING_RESTRICTION_SQE_OP;
	res[ 0 ].sqe_op = IORING_OP_READ;
	res[ 1 ].opcode = IORING_RESTRICTION_SQE_FLAGS_REQUIRED;
	res[ 1 ].sqe_flags = IOSQE_FIXED_FILE;
	res[ 2 ].opcode = IORING_RESTRICTION_REGISTER_OP;
	res[ 2 ].register_op = IORING_REGISTER_FILES_UPDATE;
	io_uring_register_restrictions( &ring, res, 3 );
	io_uring_enable_rings( &ring );

	// A read against a slot that was never filled must fail, not read something else.
	char buf[ 64 ] = { 0 };
	struct io_uring_sqe* sqe = io_uring_get_sqe( &ring );
	io_uring_prep_read( sqe, 2, buf, sizeof( buf ) - 1, 0 );
	sqe->flags |= IOSQE_FIXED_FILE;
	io_uring_submit( &ring );
	struct io_uring_cqe* cqe;
	io_uring_wait_cqe( &ring, &cqe );
	printf( "read an unfilled slot   -> %s\n", cqe->res < 0 ? strerror( -cqe->res ) : "SUCCEEDED -- BAD" );
	io_uring_cqe_seen( &ring, cqe );

	// Fill slot 2 after enable, as a call would.
	int f = open( "/etc/hostname", O_RDONLY );
	rc = io_uring_register_files_update( &ring, 2, &f, 1 );
	printf( "fill slot 2 post-enable -> %s\n", rc < 0 ? strerror( -rc ) : "ok" );

	sqe = io_uring_get_sqe( &ring );
	io_uring_prep_read( sqe, 2, buf, sizeof( buf ) - 1, 0 );
	sqe->flags |= IOSQE_FIXED_FILE;
	io_uring_submit( &ring );
	io_uring_wait_cqe( &ring, &cqe );
	if ( cqe->res < 0 )
		printf( "read slot 2             -> %s\n", strerror( -cqe->res ) );
	else
	{
		char* nl = strchr( buf, '\n' );
		if ( nl ) *nl = 0;
		printf( "read slot 2             -> \"%s\"\n", buf );
	}
	io_uring_cqe_seen( &ring, cqe );

	// And release it again, as call completion would.
	int none = -1;
	rc = io_uring_register_files_update( &ring, 2, &none, 1 );
	printf( "release slot 2           -> %s\n", rc < 0 ? strerror( -rc ) : "ok" );

	return 0;
}

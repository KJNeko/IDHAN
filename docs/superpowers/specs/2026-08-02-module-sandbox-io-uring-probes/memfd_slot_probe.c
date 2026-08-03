// Can a memfd be registered in a restricted ring's file table and read through it?
// This is the request-body path: bytes that never were a file on disk.
#define _GNU_SOURCE
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main( void )
{
	int mem = memfd_create( "probe", MFD_CLOEXEC | MFD_ALLOW_SEALING );
	const char* body = "bytes that arrived over HTTP\n";
	if ( pwrite( mem, body, strlen( body ), 0 ) < 0 ) return 1;
	fcntl( mem, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL );

	struct io_uring_params p;
	memset( &p, 0, sizeof( p ) );
	p.flags = IORING_SETUP_R_DISABLED;

	struct io_uring ring;
	if ( io_uring_queue_init_params( 8, &ring, &p ) < 0 ) return 1;

	int rc = io_uring_register_files( &ring, &mem, 1 );
	printf( "register a memfd in the file table -> %s\n", rc < 0 ? strerror( -rc ) : "ok" );

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

	char buf[ 64 ] = { 0 };
	struct io_uring_sqe* sqe = io_uring_get_sqe( &ring );
	io_uring_prep_read( sqe, 0, buf, sizeof( buf ) - 1, 0 );
	sqe->flags |= IOSQE_FIXED_FILE;
	io_uring_submit( &ring );

	struct io_uring_cqe* cqe;
	io_uring_wait_cqe( &ring, &cqe );

	if ( cqe->res < 0 )
		printf( "read the memfd through the ring -> %s\n", strerror( -cqe->res ) );
	else
		printf( "read the memfd through the ring -> %d bytes: %.*s", cqe->res, cqe->res, buf );

	return 0;
}

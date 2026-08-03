// Does a restricted io_uring survive fork+exec, and can the parent repoint its
// registered file slots afterwards while the child is denied io_uring_register?
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define RING_FD 3   // where the parent parks the inherited ring
#define SYNC_FD 4   // parent -> child: "slot repointed, read again"
#define BACK_FD 5   // child -> parent: "read done"

static void child_main( void )
{
	setvbuf( stdout, NULL, _IONBF, 0 );

	// The params travel out of band; the real design would put them in the frame.
	struct io_uring_params p;
	memset( &p, 0, sizeof( p ) );
	const char* hex = getenv( "RING_PARAMS" );
	if ( !hex )
	{
		printf( "child: no params\n" );
		_exit( 1 );
	}
	unsigned char* raw = (unsigned char*)&p;
	for ( size_t i = 0; i < sizeof( p ); ++i ) sscanf( hex + ( i * 2 ), "%2hhx", &raw[ i ] );

	struct io_uring ring;
	if ( io_uring_queue_mmap( RING_FD, &p, &ring ) < 0 )
	{
		printf( "child: queue_mmap of the inherited ring failed: %s\n", strerror( errno ) );
		_exit( 1 );
	}
	printf( "child: mapped the inherited ring\n" );

	// Close the fdinfo leak while still trusted, before any lockdown.
	if ( io_uring_register_ring_fd( &ring ) < 0 )
		printf( "child: register_ring_fd failed: %s\n", strerror( errno ) );
	else
	{
		io_uring_close_ring_fd( &ring );
		printf( "child: registered the ring and closed its descriptor\n" );
	}

	for ( int round = 0; round < 2; ++round )
	{
		char buf[ 64 ] = { 0 };
		struct io_uring_sqe* sqe = io_uring_get_sqe( &ring );
		io_uring_prep_read( sqe, 0, buf, sizeof( buf ) - 1, 0 );
		sqe->flags |= IOSQE_FIXED_FILE;
		io_uring_submit( &ring );

		struct io_uring_cqe* cqe;
		io_uring_wait_cqe( &ring, &cqe );

		if ( cqe->res < 0 )
			printf( "child: read %d failed: %s\n", round, strerror( -cqe->res ) );
		else
		{
			char* nl = strchr( buf, '\n' );
			if ( nl ) *nl = 0;
			printf( "child: slot 0 read %d -> \"%s\"\n", round, buf );
		}
		io_uring_cqe_seen( &ring, cqe );

		if ( round == 0 )
		{
			// Ask the parent to repoint the slot, then wait for it to say it has.
			char go = 'g';
			ssize_t w = write( BACK_FD, &go, 1 );
			(void)w;
			ssize_t r = read( SYNC_FD, &go, 1 );
			if ( r != 1 )
			{
				printf( "child: sync failed\n" );
				_exit( 1 );
			}
		}
	}

	// Can the child make its own ring right now?
	struct io_uring own;
	int rc = io_uring_queue_init( 4, &own, 0 );
	if ( rc < 0 )
		printf( "child: own io_uring_setup -> %s (before lockdown)\n", strerror( -rc ) );
	else
	{
		printf( "child: own io_uring_setup -> ALLOWED (before lockdown)\n" );
		io_uring_queue_exit( &own );
	}

	// Lock down: deny io_uring_setup and io_uring_register, allow everything else.
	struct sock_filter filter[] = {
		BPF_STMT( BPF_LD | BPF_W | BPF_ABS, offsetof( struct seccomp_data, nr ) ),
		BPF_JUMP( BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_setup, 2, 0 ),
		BPF_JUMP( BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_register, 1, 0 ),
		BPF_STMT( BPF_RET | BPF_K, SECCOMP_RET_ALLOW ),
		BPF_STMT( BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ( EPERM & SECCOMP_RET_DATA ) ),
	};
	struct sock_fprog prog = { .len = 5, .filter = filter };

	prctl( PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0 );
	if ( syscall( __NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog ) < 0 )
	{
		printf( "child: seccomp install failed: %s\n", strerror( errno ) );
		_exit( 1 );
	}
	printf( "child: locked down\n" );

	rc = io_uring_queue_init( 4, &own, 0 );
	if ( rc < 0 )
		printf( "child: own io_uring_setup -> DENIED (%s) (after lockdown)\n", strerror( -rc ) );
	else
		printf( "child: own io_uring_setup -> STILL ALLOWED (after lockdown) -- bad\n" );

	// And can it still repoint the slot itself?
	int sneaky = open( "/etc/passwd", O_RDONLY );
	rc = io_uring_register_files_update( &ring, 0, &sneaky, 1 );
	if ( rc < 0 )
		printf( "child: FILES_UPDATE -> DENIED (%s) (after lockdown)\n", strerror( -rc ) );
	else
		printf( "child: FILES_UPDATE -> SUCCEEDED (after lockdown) -- BAD\n" );

	// The inherited ring must still work after lockdown.
	char buf[ 64 ] = { 0 };
	struct io_uring_sqe* sqe = io_uring_get_sqe( &ring );
	io_uring_prep_read( sqe, 0, buf, sizeof( buf ) - 1, 0 );
	sqe->flags |= IOSQE_FIXED_FILE;
	io_uring_submit( &ring );
	struct io_uring_cqe* cqe;
	io_uring_wait_cqe( &ring, &cqe );
	if ( cqe->res < 0 )
		printf( "child: post-lockdown read failed: %s\n", strerror( -cqe->res ) );
	else
	{
		char* nl = strchr( buf, '\n' );
		if ( nl ) *nl = 0;
		printf( "child: post-lockdown read -> \"%s\"\n", buf );
	}

	_exit( 0 );
}

int main( int argc, char** argv )
{
	if ( argc > 1 && strcmp( argv[ 1 ], "child" ) == 0 )
	{
		child_main();
		return 0;
	}

	setvbuf( stdout, NULL, _IONBF, 0 );

	int a = open( "/etc/hostname", O_RDONLY );
	int b = open( "/etc/os-release", O_RDONLY );

	struct io_uring_params p;
	memset( &p, 0, sizeof( p ) );
	p.flags = IORING_SETUP_R_DISABLED;

	struct io_uring ring;
	if ( io_uring_queue_init_params( 8, &ring, &p ) < 0 )
	{
		printf( "parent: ring setup failed\n" );
		return 1;
	}

	// Two slots: a real worker serves concurrent calls, so one slot per in-flight call.
	int slots[ 2 ] = { a, a };
	io_uring_register_files( &ring, slots, 2 );

	struct io_uring_restriction res[ 4 ];
	memset( res, 0, sizeof( res ) );
	res[ 0 ].opcode = IORING_RESTRICTION_SQE_OP;
	res[ 0 ].sqe_op = IORING_OP_READ;
	res[ 1 ].opcode = IORING_RESTRICTION_SQE_FLAGS_REQUIRED;
	res[ 1 ].sqe_flags = IOSQE_FIXED_FILE;
	res[ 2 ].opcode = IORING_RESTRICTION_REGISTER_OP;
	res[ 2 ].register_op = IORING_REGISTER_RING_FDS;
	res[ 3 ].opcode = IORING_RESTRICTION_REGISTER_OP;
	res[ 3 ].register_op = IORING_REGISTER_FILES_UPDATE;

	if ( io_uring_register_restrictions( &ring, res, 4 ) < 0 )
	{
		printf( "parent: restrictions failed: %s\n", strerror( errno ) );
		return 1;
	}
	io_uring_enable_rings( &ring );

	char hex[ sizeof( p ) * 2 + 1 ];
	const unsigned char* raw = (const unsigned char*)&p;
	for ( size_t i = 0; i < sizeof( p ); ++i ) sprintf( hex + ( i * 2 ), "%02x", raw[ i ] );
	hex[ sizeof( p ) * 2 ] = 0;
	setenv( "RING_PARAMS", hex, 1 );

	int to_child[ 2 ], from_child[ 2 ];
	if ( pipe( to_child ) < 0 || pipe( from_child ) < 0 ) return 1;

	pid_t pid = fork();

	if ( pid == 0 )
	{
		dup2( ring.ring_fd, RING_FD );
		dup2( to_child[ 0 ], SYNC_FD );
		dup2( from_child[ 1 ], BACK_FD );
		// Clear CLOEXEC so they survive the exec.
		fcntl( RING_FD, F_SETFD, 0 );
		fcntl( SYNC_FD, F_SETFD, 0 );
		fcntl( BACK_FD, F_SETFD, 0 );
		execl( "/proc/self/exe", "inherit_probe", "child", (char*)NULL );
		_exit( 127 );
	}

	// Wait for the child's first read, then repoint slot 0 at a different file.
	char go;
	if ( read( from_child[ 0 ], &go, 1 ) == 1 )
	{
		if ( io_uring_register_files_update( &ring, 0, &b, 1 ) < 0 )
			printf( "parent: FILES_UPDATE -> %s\n", strerror( errno ) );
		else
			printf( "parent: FILES_UPDATE -> slot 0 repointed\n" );

		ssize_t w = write( to_child[ 1 ], &go, 1 );
		(void)w;
	}

	wait( NULL );

	return 0;
}

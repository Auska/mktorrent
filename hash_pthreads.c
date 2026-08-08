/*
This file is part of mktorrent
Copyright (C) 2007, 2009 Emil Renner Berthing

mktorrent is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

mktorrent is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/


#include <stdlib.h>       /* exit(), malloc() */
#include <sys/types.h>    /* off_t */
#include <errno.h>        /* errno */
#include <string.h>       /* strerror() */
#include <stdio.h>        /* printf() etc. */
#include <fcntl.h>        /* open() */
#include <unistd.h>       /* read(), close() */
#include <inttypes.h>     /* PRId64 etc. */
#include <pthread.h>
#include <time.h>         /* nanosleep() */

#ifdef USE_OPENSSL
#include <openssl/evp.h>  /* EVP_MD_CTX */
#include <openssl/sha.h>  /* SHA_DIGEST_LENGTH */
#else
#include "sha1.h"
#endif

#include "export.h"
#include "mktorrent.h"
#include "hash.h"
#include "msg.h"

#ifndef PROGRESS_PERIOD
#define PROGRESS_PERIOD 200000
#endif

/* total budget for in-flight piece buffers, in bytes */
#ifndef BUFFER_MEMORY_LIMIT
#define BUFFER_MEMORY_LIMIT (64U * 1024U * 1024U)
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if defined _LARGEFILE_SOURCE && defined O_LARGEFILE
#define OPENFLAGS (O_RDONLY | O_BINARY | O_LARGEFILE)
#else
#define OPENFLAGS (O_RDONLY | O_BINARY)
#endif


struct piece {
	struct piece *next;
	unsigned char *dest;
	unsigned long len;
	unsigned char data[1];
};

struct queue {
	struct piece *free;
	struct piece *full;
	unsigned int buffers_max;
	unsigned int buffers;
	pthread_mutex_t mutex_free;
	pthread_mutex_t mutex_full;
	pthread_cond_t cond_empty;
	pthread_cond_t cond_full;
	unsigned int done;
	unsigned int pieces;
	/* read by the progress thread without holding any mutex */
	_Atomic unsigned int pieces_hashed;
	_Atomic int stop;
};

static struct piece *get_free(struct queue *q, size_t piece_length)
{
	struct piece *r;

	pthread_mutex_lock(&q->mutex_free);
	if (q->free) {
		r = q->free;
		q->free = r->next;
	} else if (q->buffers < q->buffers_max) {
		r = malloc(sizeof(struct piece) - 1 + piece_length);
		FATAL_IF0(r == NULL, "out of memory\n");


		q->buffers++;
	} else {
		while (q->free == NULL) {
			pthread_cond_wait(&q->cond_full, &q->mutex_free);
		}

		r = q->free;
		q->free = r->next;
	}
	pthread_mutex_unlock(&q->mutex_free);

	return r;
}

static struct piece *get_full(struct queue *q)
{
	struct piece *r;

	pthread_mutex_lock(&q->mutex_full);
again:
	if (q->full) {
		r = q->full;
		q->full = r->next;
	} else if (q->done) {
		r = NULL;
	} else {
		pthread_cond_wait(&q->cond_empty, &q->mutex_full);
		goto again;
	}
	pthread_mutex_unlock(&q->mutex_full);

	return r;
}

static void put_free(struct queue *q, struct piece *p, unsigned int hashed)
{
	pthread_mutex_lock(&q->mutex_free);
	p->next = q->free;
	q->free = p;
	q->pieces_hashed += hashed;
	pthread_mutex_unlock(&q->mutex_free);
	pthread_cond_signal(&q->cond_full);
}

static void put_full(struct queue *q, struct piece *p)
{
	pthread_mutex_lock(&q->mutex_full);
	p->next = q->full;
	q->full = p;
	pthread_mutex_unlock(&q->mutex_full);
	pthread_cond_signal(&q->cond_empty);
}

static void set_done(struct queue *q)
{
	pthread_mutex_lock(&q->mutex_full);
	q->done = 1;
	pthread_mutex_unlock(&q->mutex_full);
	pthread_cond_broadcast(&q->cond_empty);
}

static void free_buffers(struct queue *q)
{
	struct piece *first = q->free;

	while (first) {
		struct piece *p = first;
		first = p->next;
		free(p);
	}

	q->free = NULL;
}

/*
 * print the progress in a thread of its own
 */
static void *print_progress(void *data)
{
	struct queue *q = data;
	struct timespec t;

	t.tv_sec = PROGRESS_PERIOD / 1000000;
	t.tv_nsec = PROGRESS_PERIOD % 1000000 * 1000;

	while (!q->stop) {
		/* print progress and flush the buffer immediately */
		printf("\rHashed %u of %u pieces.", q->pieces_hashed, q->pieces);
		fflush(stdout);
		/* now sleep for PROGRESS_PERIOD microseconds */
		nanosleep(&t, NULL);
	}

	return NULL;
}

static void *worker(void *data)
{
	struct queue *q = data;
	struct piece *p;
#ifdef USE_OPENSSL
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	FATAL_IF0(ctx == NULL, "cannot create EVP context\n");

	while ((p = get_full(q))) {
		EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
		EVP_DigestUpdate(ctx, p->data, p->len);
		EVP_DigestFinal_ex(ctx, p->dest, NULL);
		put_free(q, p, 1);
	}

	EVP_MD_CTX_free(ctx);
#else
	SHA_CTX c;

	while ((p = get_full(q))) {
		SHA1_Init(&c);
		SHA1_Update(&c, p->data, p->len);
		SHA1_Final(p->dest, &c);
		put_free(q, p, 1);
	}
#endif

	return NULL;
}

static void read_files(struct metafile *m, struct queue *q, unsigned char *pos)
{
	int fd;                /* file descriptor */
	size_t r = 0;          /* number of bytes read from file(s)
	                          into the read buffer */
#ifndef NO_HASH_CHECK
	uintmax_t counter = 0; /* number of bytes hashed
	                          should match size when done */
#endif
	struct piece *p = get_free(q, m->piece_length);

	/* go through all the files in the file list */
	LL_FOR(file_node, m->file_list) {
		struct file_data *f = LL_DATA_AS(file_node, struct file_data*);

		/* open the current file for reading */
		FATAL_IF((fd = open(f->path, OPENFLAGS)) == -1,
			"cannot open '%s' for reading: %s\n", f->path, strerror(errno));

		while (1) {
			ssize_t d = read(fd, p->data + r, m->piece_length - r);

			FATAL_IF(d < 0, "cannot read from '%s': %s\n",
				f->path, strerror(errno));

			if (d == 0) /* end of file */
				break;

			r += d;

			if (r == m->piece_length) {
				p->dest = pos;
				p->len = m->piece_length;
				put_full(q, p);
				pos += SHA_DIGEST_LENGTH;
#ifndef NO_HASH_CHECK
				counter += r;
#endif
				r = 0;
				p = get_free(q, m->piece_length);
			}
		}

		/* now close the file */
		FATAL_IF(close(fd), "cannot close '%s': %s\n",
			f->path, strerror(errno));
	}

	/* finally append the hash of the last irregular piece to the hash string */
	if (r) {
		p->dest = pos;
		p->len = r;
		put_full(q, p);
	} else
		put_free(q, p, 0);

#ifndef NO_HASH_CHECK
	counter += r;
	FATAL_IF(counter != m->size,
		"counted %" PRIuMAX " bytes, but hashed %" PRIuMAX " bytes; "
		"something is wrong...\n",
			m->size, counter);
#endif
}

EXPORT unsigned char *make_hash(struct metafile *m)
{
	struct queue q = {
		.mutex_free  = PTHREAD_MUTEX_INITIALIZER,
		.mutex_full  = PTHREAD_MUTEX_INITIALIZER,
		.cond_empty  = PTHREAD_COND_INITIALIZER,
		.cond_full   = PTHREAD_COND_INITIALIZER,
	};
	pthread_t print_progress_thread;	/* progress printer thread */
	pthread_t *workers;
	unsigned char *hash_string;		/* the hash string */
	int i;
	int err;

	workers = malloc(m->threads * sizeof(pthread_t));
	hash_string = malloc((size_t)m->pieces * SHA_DIGEST_LENGTH);
	FATAL_IF0(workers == NULL || hash_string == NULL, "out of memory\n");

	q.pieces = m->pieces;
	/* keep up to 3 in-flight buffers per worker, but never more than the
	   total memory budget allows, and at least one per worker so the
	   threads stay busy. Without the budget, huge piece lengths would
	   allocate 3*threads*piece_length bytes (e.g. 20 threads with 256 MiB
	   pieces would grab ~15 GiB). */
	q.buffers_max = 3 * (unsigned int)m->threads;
	unsigned int max_by_memory = BUFFER_MEMORY_LIMIT / m->piece_length;
	if (q.buffers_max > max_by_memory)
		q.buffers_max = max_by_memory;
	if (q.buffers_max < (unsigned int)m->threads)
		q.buffers_max = (unsigned int)m->threads;

	/* create worker threads */
	for (i = 0; i < m->threads; i++) {
		err = pthread_create(&workers[i], NULL, worker, &q);
		FATAL_IF(err, "cannot create thread: %s\n", strerror(err));
	}

	/* now set off the progress printer */
	err = pthread_create(&print_progress_thread, NULL, print_progress, &q);
	FATAL_IF(err, "cannot create thread: %s\n", strerror(err));

	/* read files and feed pieces to the workers */
	read_files(m, &q, hash_string);

	/* we're done so stop printing our progress. */
	q.stop = 1;

	/* inform workers we're done */
	set_done(&q);

	/* wait for workers to finish */
	for (i = 0; i < m->threads; i++) {
		err = pthread_join(workers[i], NULL);
		FATAL_IF(err, "cannot join thread: %s\n", strerror(err));
	}

	free(workers);

	/* the progress printer should be done by now too */
	err = pthread_join(print_progress_thread, NULL);
	FATAL_IF(err, "cannot join thread: %s\n", strerror(err));

	/* destroy mutexes and condition variables */
	pthread_mutex_destroy(&q.mutex_full);
	pthread_mutex_destroy(&q.mutex_free);
	pthread_cond_destroy(&q.cond_empty);
	pthread_cond_destroy(&q.cond_full);

	/* free buffers */
	free_buffers(&q);

	/* ok, let the user know we're done too */
	printf("\rhashed %u of %u pieces\n", q.pieces_hashed, q.pieces);

	return hash_string;
}

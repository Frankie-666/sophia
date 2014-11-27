
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <libsr.h>
#include <libsv.h>
#include <libsm.h>
#include <libsl.h>
#include <libsd.h>
#include <libsi.h>
#include <libse.h>
#include <libso.h>

int so_dbctl_init(sodbctl *c, char *name, void *db)
{
	memset(c, 0, sizeof(*c));
	sodb *o = db;
	c->name = sr_strdup(&o->e->a, name);
	if (srunlikely(c->name == NULL)) {
		sr_error(&o->e->error, "%s", "memory allocation failed");
		sr_error_recoverable(&o->e->error);
		return -1;
	}
	c->parent     = db;
	c->created    = 0;
	c->sync       = 1;
	c->cmp.cmp    = sr_cmpstring;
	c->cmp.cmparg = NULL;
	return 0;
}

int so_dbctl_free(sodbctl *c)
{
	sodb *o = c->parent;
	if (so_dbactive(o))
		return -1;
	if (c->name) {
		sr_free(&o->e->a, c->name);
		c->name = NULL;
	}
	if (c->path) {
		sr_free(&o->e->a, c->path);
		c->path = NULL;
	}
	return 0;
}

int so_dbctl_validate(sodbctl *c)
{
	sodb *o = c->parent;
	so *e = o->e;
	if (c->path)
		return 0;
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", e->ctl.path, c->name);
	c->path = sr_strdup(&e->a, path);
	if (srunlikely(c->path == NULL)) {
		sr_error(&e->error, "%s", "memory allocation failed");
		sr_error_recoverable(&e->error);
		return -1;
	}
	return 0;
}

static int
so_dbctl_branch(srctl *c srunused, void *arg, va_list args srunused)
{
	return so_scheduler_branch(arg);
}

static int
so_dbctl_compact(srctl *c srunused, void *arg, va_list args srunused)
{
	return so_scheduler_compact(arg);
}

static int
so_dbctl_lockdetect(srctl *c srunused, void *arg, va_list args)
{
	sodb *db = arg;
	sotx *tx = va_arg(args, sotx*);
	if (srunlikely(tx->db != db)) {
		sr_error(&db->e->error, "%s", "transaction does not match a parent db object");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	int rc = sm_deadlock(&tx->t);
	return rc;
}

typedef struct sodbctlinfo sodbctlinfo;

struct sodbctlinfo {
	char *status;
};

static inline void
so_dbctl_info(sodb *db, sodbctlinfo *info)
{
	info->status = so_statusof(&db->status);
}

static inline void
so_dbctl_prepare(srctl *t, sodbctl *c, sodbctlinfo *info)
{
	sodb *db = c->parent;
	so_dbctl_info(db, info);
	srctl *p = t;
	p = sr_ctladd(p, "name",            SR_CTLSTRING|SR_CTLRO, c->name,       NULL);
	p = sr_ctladd(p, "id",              SR_CTLU32,             &c->id,        NULL);
	p = sr_ctladd(p, "status",          SR_CTLSTRING|SR_CTLRO, info->status,  NULL);
	p = sr_ctladd(p, "path",            SR_CTLSTRINGREF,       &c->path,      NULL);
	p = sr_ctladd(p, "sync",            SR_CTLINT,             &c->sync,      NULL);
	p = sr_ctladd(p, "branch",          SR_CTLTRIGGER,         NULL,          so_dbctl_branch);
	p = sr_ctladd(p, "compact",         SR_CTLTRIGGER,         NULL,          so_dbctl_compact);
	p = sr_ctladd(p, "lockdetect",      SR_CTLTRIGGER,         NULL,          so_dbctl_lockdetect);
	p = sr_ctladd(p, "index",           SR_CTLSUB,             NULL,          NULL);
	p = sr_ctladd(p, "error_injection", SR_CTLSUB,             NULL,          NULL);
	p = sr_ctladd(p,  NULL,             0,                     NULL,          NULL);
}

static int
so_dbindex_cmp(srctl *c srunused, void *arg, va_list args)
{
	sodb *db = arg;
	db->ctl.cmp.cmp = va_arg(args, srcmpf);
	return 0;
}

static int
so_dbindex_cmparg(srctl *c srunused, void *arg, va_list args srunused)
{
	sodb *db = arg;
	db->ctl.cmp.cmparg = va_arg(args, void*);
	return 0;
}

static inline void
so_dbindex_prepare(srctl *t, siprofiler *pf)
{
	srctl *p = t;
	p = sr_ctladd(p, "cmp",              SR_CTLTRIGGER,         NULL,                     so_dbindex_cmp);
	p = sr_ctladd(p, "cmp_arg",          SR_CTLTRIGGER,         NULL,                     so_dbindex_cmparg);
	p = sr_ctladd(p, "node_count",       SR_CTLU32|SR_CTLRO,    &pf->total_node_count,    NULL);
	p = sr_ctladd(p, "node_size",        SR_CTLU64|SR_CTLRO,    &pf->total_node_size,     NULL);
	p = sr_ctladd(p, "branch_count",     SR_CTLU32|SR_CTLRO,    &pf->total_branch_count,  NULL);
	p = sr_ctladd(p, "branch_avg",       SR_CTLU32|SR_CTLRO,    &pf->total_branch_avg,    NULL);
	p = sr_ctladd(p, "branch_max",       SR_CTLU32|SR_CTLRO,    &pf->total_branch_max,    NULL);
	p = sr_ctladd(p, "branch_size",      SR_CTLU64|SR_CTLRO,    &pf->total_branch_size,   NULL);
	p = sr_ctladd(p, "memory_used",      SR_CTLU64|SR_CTLRO,    &pf->memory_used,         NULL);
	p = sr_ctladd(p, "count",            SR_CTLU64|SR_CTLRO,    &pf->count,               NULL);
	p = sr_ctladd(p, "seq_dsn",          SR_CTLU32|SR_CTLRO,    &pf->seq.dsn,             NULL);
	p = sr_ctladd(p, "seq_nsn",          SR_CTLU32|SR_CTLRO,    &pf->seq.nsn,             NULL);
	p = sr_ctladd(p, "seq_lsn",          SR_CTLU64|SR_CTLRO,    &pf->seq.lsn,             NULL);
	p = sr_ctladd(p, "seq_lfsn",         SR_CTLU32|SR_CTLRO,    &pf->seq.lfsn,            NULL);
	p = sr_ctladd(p, "seq_tsn",          SR_CTLU32|SR_CTLRO,    &pf->seq.tsn,             NULL);
	p = sr_ctladd(p, "histogram_branch", SR_CTLSTRING|SR_CTLRO, pf->histogram_branch_ptr, NULL);
	p = sr_ctladd(p,  NULL,              0,                     NULL,                     NULL);
}

static inline void
so_dbei_prepare(srctl *t, srinjection *i)
{
	srctl *p = t;
	p = sr_ctladd(p, "si_branch_0",     SR_CTLINT, &i->e[0], NULL);
	p = sr_ctladd(p, "si_branch_1",     SR_CTLINT, &i->e[1], NULL);
	p = sr_ctladd(p, "si_compaction_0", SR_CTLINT, &i->e[2], NULL);
	p = sr_ctladd(p, "si_compaction_1", SR_CTLINT, &i->e[3], NULL);
	p = sr_ctladd(p, "si_compaction_2", SR_CTLINT, &i->e[4], NULL);
	p = sr_ctladd(p, "si_compaction_3", SR_CTLINT, &i->e[5], NULL);
	p = sr_ctladd(p, "si_compaction_4", SR_CTLINT, &i->e[6], NULL);
	p = sr_ctladd(p,  NULL,             0,          NULL,    NULL);
}

static int
so_dbindex_set(sodb *db, char *path, va_list args)
{
	siprofiler pf;
	si_profilerbegin(&pf, &db->index);
	si_profiler(&pf, &db->r);
	si_profilerend(&pf);
	srctl ctls[30];
	so_dbindex_prepare(&ctls[0], &pf);
	srctl *match = NULL;
	int rc = sr_ctlget(&ctls[0], &path, &match);
	if (srunlikely(rc ==  1))
		return -1; /* self */
	if (srunlikely(rc == -1)) {
		sr_error(&db->e->error, "%s", "bad control path");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	int type = match->type & ~SR_CTLRO;
	if (so_dbactive(db) && (type != SR_CTLTRIGGER)) {
		sr_error(&db->e->error, "%s", "failed to set control path");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	rc = sr_ctlset(match, db->r.a, db, args);
	if (srunlikely(rc == -1)) {
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	return rc;
}

static void*
so_dbindex_get(sodb *db, char *path)
{
	siprofiler pf;
	si_profilerbegin(&pf, &db->index);
	si_profiler(&pf, &db->r);
	si_profilerend(&pf);
	srctl ctls[30];
	so_dbindex_prepare(&ctls[0], &pf);
	srctl *match = NULL;
	int rc = sr_ctlget(&ctls[0], &path, &match);
	if (srunlikely(rc == 1 || rc == -1)) {
		sr_error(&db->e->error, "%s", "bad control path");
		sr_error_recoverable(&db->e->error);
		return NULL;
	}
	return so_ctlreturn(match, db->e);
}

static int
so_dbindex_dump(sodb *db, srbuf *dump)
{
	siprofiler pf;
	si_profilerbegin(&pf, &db->index);
	si_profiler(&pf, &db->r);
	si_profilerend(&pf);
	srctl ctls[30];
	so_dbindex_prepare(&ctls[0], &pf);
	char prefix[64];
	snprintf(prefix, sizeof(prefix), "db.%s.index.", db->ctl.name);
	int rc = sr_ctlserialize(&ctls[0], &db->e->a, prefix, dump);
	if (srunlikely(rc == -1)) {
		sr_error(&db->e->error, "%s", "memory allocation failed");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	return 0;
}

static int
so_dbei_set(sodb *db, char *path, va_list args)
{
	srctl ctls[30];
	so_dbei_prepare(&ctls[0], &db->ei);
	srctl *match = NULL;
	int rc = sr_ctlget(&ctls[0], &path, &match);
	if (srunlikely(rc == 1 || rc == -1)) {
		sr_error(&db->e->error, "%s", "bad control path");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	rc = sr_ctlset(match, db->r.a, db, args);
	if (srunlikely(rc == -1)) {
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	return rc;
}

static void*
so_dbei_get(sodb *db, char *path)
{
	srctl ctls[30];
	so_dbei_prepare(&ctls[0], &db->ei);
	srctl *match = NULL;
	int rc = sr_ctlget(&ctls[0], &path, &match);
	if (srunlikely(rc == 1 || rc == -1)) {
		sr_error(&db->e->error, "%s", "bad control path");
		sr_error_recoverable(&db->e->error);
		return NULL;
	}
	return so_ctlreturn(match, db->e);
}

#if 0
static int
so_dbei_dump(sodb *db, srbuf *dump)
{
	srctl ctls[30];
	so_dbei_prepare(&ctls[0], &db->ei);
	char prefix[64];
	snprintf(prefix, sizeof(prefix), "db.%s.error_injection.", db->ctl.name);
	int rc = sr_ctlserialize(&ctls[0], &db->e->a, prefix, dump);
	if (srunlikely(rc == -1)) {
		sr_error(&db->e->error, "%s", "memory allocation failed");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	return 0;
}
#endif

int so_dbctl_set(sodbctl *c, char *path, va_list args)
{
	sodb *db = c->parent;
	srctl ctls[30];
	sodbctlinfo info;
	so_dbctl_prepare(&ctls[0], c, &info);
	srctl *match = NULL;
	int rc = sr_ctlget(&ctls[0], &path, &match);
	if (srunlikely(rc ==  1))
		return  0; /* self */
	if (srunlikely(rc == -1)) {
		sr_error(&db->e->error, "%s", "bad control path");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	int type = match->type & ~SR_CTLRO;
	if (type == SR_CTLSUB) {
		if (strcmp(match->name, "index") == 0)
			return so_dbindex_set(db, path, args);
		else
		if (strcmp(match->name, "error_injection") == 0)
			return so_dbei_set(db, path, args);
	}
	if (so_dbactive(db) && (type != SR_CTLTRIGGER)) {
		sr_error(&db->e->error, "%s", "failed to set control path");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	rc = sr_ctlset(match, db->r.a, db, args);
	if (srunlikely(rc == -1)) {
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	return rc;
}

void *so_dbctl_get(sodbctl *c, char *path, va_list args srunused)
{
	sodb *db = c->parent;
	srctl ctls[30];
	sodbctlinfo info;
	so_dbctl_prepare(&ctls[0], c, &info);
	srctl *match = NULL;
	int rc = sr_ctlget(&ctls[0], &path, &match);
	if (srunlikely(rc ==  1))
		return &db->o; /* self */
	if (srunlikely(rc == -1)) {
		sr_error(&db->e->error, "%s", "bad control path");
		sr_error_recoverable(&db->e->error);
		return NULL;
	}
	int type = match->type & ~SR_CTLRO;
	if (type == SR_CTLSUB) {
		if (strcmp(match->name, "index") == 0)
			return so_dbindex_get(db, path);
		else
		if (strcmp(match->name, "error_injection") == 0)
			return so_dbei_get(db, path);
		sr_error(&db->e->error, "%s", "unknown control path");
		sr_error_recoverable(&db->e->error);
		return NULL;
	}
	return so_ctlreturn(match, db->e);
}

int so_dbctl_dump(sodbctl *c, srbuf *dump)
{
	sodb *db = c->parent;
	srctl ctls[30];
	sodbctlinfo info;
	so_dbctl_prepare(&ctls[0], c, &info);
	char prefix[64];
	snprintf(prefix, sizeof(prefix), "db.%s.", c->name);
	int rc = sr_ctlserialize(&ctls[0], &db->e->a, prefix, dump);
	if (srunlikely(rc == -1)) {
		sr_error(&db->e->error, "%s", "memory allocation failed");
		sr_error_recoverable(&db->e->error);
		return -1;
	}
	rc = so_dbindex_dump(db, dump);
	if (srunlikely(rc == -1))
		return -1;
	return 0;
}

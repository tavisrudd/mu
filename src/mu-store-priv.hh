/* -*-mode: c++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8-*- */
/*
** Copyright (C) 2011  <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/

#ifndef __MU_STORE_PRIV_HH__
#define __MU_STORE_PRIV_HH__

#if HAVE_CONFIG_H
#include "config.h"
#endif /*HAVE_CONFIG_H*/

#include <cstdio>
#include <xapian.h>
#include <cstring>
#include <stdexcept>

#include "mu-store.h"
#include "mu-contacts.h"


class MuStoreError {
public:
	MuStoreError (MuError err, const std::string& what) :
		_err (err), _what(what) {}
	MuError mu_error () const { return _err; }
	const std::string& what() const { return _what; }
private:
	MuError _err;
	const std::string _what;
};


struct _MuStore {
public:
	/* create a read-write MuStore */
	_MuStore (const char *path, const char *contacts_path, bool rebuild) {

		init (path, contacts_path, rebuild, false);

		if (rebuild)
			_db = new Xapian::WritableDatabase
				(path, Xapian::DB_CREATE_OR_OVERWRITE);
		else
			_db = new Xapian::WritableDatabase
				(path, Xapian::DB_CREATE_OR_OPEN);

		check_set_version ();

		if (contacts_path) {
			_contacts = mu_contacts_new (contacts_path);
			if (!_contacts) /* don't bail-out for this */
				throw MuStoreError (MU_ERROR_FILE,
					    ("failed to init contacts cache"));
		}

		MU_WRITE_LOG ("%s: opened %s (batch size: %u) for read-write",
			      __FUNCTION__, this->path(), batch_size());
	}

	/* create a read-only MuStore */
	_MuStore (const char *path) {

		init (path, NULL, false, false);

		_db = new Xapian::Database (path);
		if (mu_store_needs_upgrade(this))
			throw MuStoreError (MU_ERROR_XAPIAN_NOT_UP_TO_DATE,
					    ("store needs an upgrade"));

		MU_WRITE_LOG ("%s: opened %s read-only", __FUNCTION__, this->path());
	}

	void init (const char *path, const char *contacts_path,
		   bool rebuild, bool read_only) {

		_batch_size	= DEFAULT_BATCH_SIZE;
		_contacts       = 0;
		_in_transaction = false;
		_path           = path;
		_processed	= 0;
		_read_only      = read_only;
		_ref_count      = 1;
		_version        = NULL;
	}

	void check_set_version () {
		/* check version...*/
		gchar *version;
		version = mu_store_get_metadata (this, MU_STORE_VERSION_KEY, NULL);
		if (!version)
			mu_store_set_metadata (this, MU_STORE_VERSION_KEY,
					       MU_STORE_SCHEMA_VERSION, NULL);
		else if (g_strcmp0 (version, MU_STORE_SCHEMA_VERSION) != 0) {
			g_free (version);
			throw MuStoreError (MU_ERROR_XAPIAN_NOT_UP_TO_DATE,
					    ("store needs an upgrade"));
		} else
			g_free (version);
	}

	~_MuStore () {
		try {
			if (_ref_count != 0)
				g_warning ("ref count != 0");

			g_free (_version);

			mu_contacts_destroy (_contacts);
			if (!_read_only)
				mu_store_flush (this);

			MU_WRITE_LOG ("closing xapian database with %d document(s)",
				      (int)db_read_only()->get_doccount());
			delete _db;

		} MU_XAPIAN_CATCH_BLOCK;
	}

	/* close the old database, and write an empty one on top of it */
	void clear () {
		if (is_read_only())
			throw std::runtime_error ("database is read-only");

		// clear the database
		db_writable()->close ();
		delete _db;
		_db = new Xapian::WritableDatabase
			(path(), Xapian::DB_CREATE_OR_OVERWRITE);

		// clear the contacts cache
		if (_contacts)
			mu_contacts_clear (_contacts);
	}

	/* get a unique id for this message; note, this function returns a
	 * static buffer -- not reentrant */
	const char *get_uid_term (const char *path);

	MuContacts* contacts() { return _contacts; }

	const char* version ()  {
		g_free (_version);
		return _version = mu_store_get_metadata (this, MU_STORE_VERSION_KEY,
							 NULL);
	}

	void set_version (const char *version)  {
		mu_store_set_metadata (this, MU_STORE_VERSION_KEY, version, NULL);
	}

	static unsigned max_term_length() { return MAX_TERM_LENGTH; }

	void begin_transaction ();
	void commit_transaction ();
	void rollback_transaction ();

	Xapian::WritableDatabase* db_writable() {
		if (G_UNLIKELY(is_read_only()))
			throw std::runtime_error ("database is read-only");
		return (Xapian::WritableDatabase*)_db;
	}

	Xapian::Database* db_read_only() const { return _db; }

	const char* path () const { return _path.c_str(); }
	bool is_read_only () const { return _read_only; }

	size_t batch_size () const { return _batch_size;}
	size_t set_batch_size (size_t n)  {
		return _batch_size = ( n == 0) ? DEFAULT_BATCH_SIZE : n;
	}

	bool   in_transaction () const { return _in_transaction; }
	bool   in_transaction (bool in_tx) { return _in_transaction = in_tx; }

	int    processed () const { return _processed; }
	int    set_processed (int n) { return _processed = n;}
	int    inc_processed () { return ++_processed; }

	/* MuStore is ref-counted */
	guint  ref   () { return ++_ref_count; }
	guint  unref () {
		if (_ref_count < 1)
			g_critical ("ref count error");
		return --_ref_count;
	}

	/* by default, use transactions of 30000 messages */
	static const unsigned DEFAULT_BATCH_SIZE = 30000;
	/* http://article.gmane.org/gmane.comp.search.xapian.general/3656 */
	static const unsigned MAX_TERM_LENGTH = 240;

private:
	/* transaction handling */
	bool   _in_transaction;
	int    _processed;
	size_t  _batch_size;  /* batch size of a xapian transaction */

	/* contacts object to cache all the contact information */
	MuContacts *_contacts;

	std::string _path;
	gchar *_version;

	Xapian::Database *_db;
	bool _read_only;

	guint _ref_count;
};


#endif /*__MU_STORE_PRIV_HH__*/

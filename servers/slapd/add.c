/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>
#include <ac/time.h>
#include <ac/socket.h>

#include "ldap_pvt.h"
#include "slap.h"

#ifdef LDAP_SLAPI
#include "slapi.h"
static void initAddPlugin( Operation *op,
	struct berval *dn, Entry *e, int manageDSAit );
static int doPreAddPluginFNs( Operation *op );
static void doPostAddPluginFNs( Operation *op );
#endif /* LDAP_SLAPI */

int
do_add( Operation *op, SlapReply *rs )
{
	BerElement	*ber = op->o_ber;
	char		*last;
	struct berval dn = { 0, NULL };
	ber_len_t	len;
	ber_tag_t	tag;
	Entry		*e;
	Modifications	*modlist = NULL;
	Modifications	**modtail = &modlist;
	Modifications	tmp;
	int	manageDSAit;

#ifdef NEW_LOGGING
	LDAP_LOG( OPERATION, ENTRY, "do_add: conn %d enter\n", op->o_connid,0,0 );
#else
	Debug( LDAP_DEBUG_TRACE, "do_add\n", 0, 0, 0 );
#endif
	/*
	 * Parse the add request.  It looks like this:
	 *
	 *	AddRequest := [APPLICATION 14] SEQUENCE {
	 *		name	DistinguishedName,
	 *		attrs	SEQUENCE OF SEQUENCE {
	 *			type	AttributeType,
	 *			values	SET OF AttributeValue
	 *		}
	 *	}
	 */

	/* get the name */
	if ( ber_scanf( ber, "{m", /*}*/ &dn ) == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR, 
			"do_add: conn %d ber_scanf failed\n", op->o_connid,0,0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_add: ber_scanf failed\n", 0, 0, 0 );
#endif
		send_ldap_discon( op, rs, LDAP_PROTOCOL_ERROR, "decoding error" );
		return -1;
	}

	e = (Entry *) ch_calloc( 1, sizeof(Entry) );

	rs->sr_err = dnPrettyNormal( NULL, &dn, &op->o_req_dn, &op->o_req_ndn, op->o_tmpmemctx );

	if( rs->sr_err != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR, 
			"do_add: conn %d invalid dn (%s)\n", op->o_connid, dn.bv_val, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_add: invalid dn (%s)\n", dn.bv_val, 0, 0 );
#endif
		send_ldap_error( op, rs, LDAP_INVALID_DN_SYNTAX, "invalid DN" );
		goto done;
	}

	ber_dupbv( &e->e_name, &op->o_req_dn );
	ber_dupbv( &e->e_nname, &op->o_req_ndn );

#ifdef NEW_LOGGING
	LDAP_LOG( OPERATION, ARGS, 
		"do_add: conn %d  dn (%s)\n", op->o_connid, e->e_dn, 0 );
#else
	Debug( LDAP_DEBUG_ARGS, "do_add: dn (%s)\n", e->e_dn, 0, 0 );
#endif

	/* get the attrs */
	for ( tag = ber_first_element( ber, &len, &last ); tag != LBER_DEFAULT;
	    tag = ber_next_element( ber, &len, last ) )
	{
		Modifications *mod;
		ber_tag_t rtag;

		tmp.sml_nvalues = NULL;

		rtag = ber_scanf( ber, "{m{W}}", &tmp.sml_type, &tmp.sml_values );

		if ( rtag == LBER_ERROR ) {
#ifdef NEW_LOGGING
			LDAP_LOG( OPERATION, ERR, 
				   "do_add: conn %d	 decoding error \n", op->o_connid, 0, 0 );
#else
			Debug( LDAP_DEBUG_ANY, "do_add: decoding error\n", 0, 0, 0 );
#endif
			send_ldap_discon( op, rs, LDAP_PROTOCOL_ERROR, "decoding error" );
			rs->sr_err = -1;
			goto done;
		}

		if ( tmp.sml_values == NULL ) {
#ifdef NEW_LOGGING
			LDAP_LOG( OPERATION, INFO, 
				"do_add: conn %d	 no values for type %s\n",
				op->o_connid, tmp.sml_type.bv_val, 0 );
#else
			Debug( LDAP_DEBUG_ANY, "no values for type %s\n",
				tmp.sml_type.bv_val, 0, 0 );
#endif
			send_ldap_error( op, rs, LDAP_PROTOCOL_ERROR,
				"no values for attribute type" );
			goto done;
		}

		mod  = (Modifications *) ch_malloc( sizeof(Modifications) );
		mod->sml_op = LDAP_MOD_ADD;
		mod->sml_next = NULL;
		mod->sml_desc = NULL;
		mod->sml_type = tmp.sml_type;
		mod->sml_values = tmp.sml_values;
		mod->sml_nvalues = NULL;

		*modtail = mod;
		modtail = &mod->sml_next;
	}

	if ( ber_scanf( ber, /*{*/ "}") == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, ERR, 
			"do_add: conn %d ber_scanf failed\n", op->o_connid, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_add: ber_scanf failed\n", 0, 0, 0 );
#endif
		send_ldap_discon( op, rs, LDAP_PROTOCOL_ERROR, "decoding error" );
		rs->sr_err = -1;
		goto done;
	}

	if( get_ctrls( op, rs, 1 ) != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, INFO, 
			"do_add: conn %d get_ctrls failed\n", op->o_connid, 0, 0 );
#else
		Debug( LDAP_DEBUG_ANY, "do_add: get_ctrls failed\n", 0, 0, 0 );
#endif
		goto done;
	} 

	if ( modlist == NULL ) {
		send_ldap_error( op, rs, LDAP_PROTOCOL_ERROR,
			"no attributes provided" );
		goto done;
	}

	Statslog( LDAP_DEBUG_STATS, "conn=%lu op=%lu ADD dn=\"%s\"\n",
	    op->o_connid, op->o_opid, e->e_name.bv_val, 0, 0 );

	if( e->e_nname.bv_len == 0 ) {
		/* protocolError may be a more appropriate error */
		send_ldap_error( op, rs, LDAP_ALREADY_EXISTS,
			"root DSE already exists" );
		goto done;

	} else if ( bvmatch( &e->e_nname, &global_schemandn ) ) {
		send_ldap_error( op, rs, LDAP_ALREADY_EXISTS,
			"subschema subentry already exists" );
		goto done;
	}

	manageDSAit = get_manageDSAit( op );

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one, or send a referral to our "referral server"
	 * if we don't hold it.
	 */
	op->o_bd = select_backend( &e->e_nname, manageDSAit, 0 );
	if ( op->o_bd == NULL ) {
		rs->sr_ref = referral_rewrite( default_referral,
			NULL, &e->e_name, LDAP_SCOPE_DEFAULT );
		if (!rs->sr_ref) rs->sr_ref = default_referral;
		if ( rs->sr_ref != NULL ) {
			rs->sr_err = LDAP_REFERRAL;
			send_ldap_result( op, rs );

			if ( rs->sr_ref != default_referral ) {
				ber_bvarray_free( rs->sr_ref );
			}
		} else {
			send_ldap_error( op, rs, LDAP_UNWILLING_TO_PERFORM,
					"referral missing" );
		}
		goto done;
	}

	/* check restrictions */
	if( backend_check_restrictions( op, rs, NULL ) != LDAP_SUCCESS ) {
		send_ldap_result( op, rs );
		goto done;
	}

	/* check for referrals */
	if( backend_check_referrals( op, rs ) != LDAP_SUCCESS ) {
		goto done;
	}

#ifdef LDAP_SLAPI
	initAddPlugin( op, &dn, e, manageDSAit );
#endif /* LDAP_SLAPI */

	/*
	 * do the add if 1 && (2 || 3)
	 * 1) there is an add function implemented in this backend;
	 * 2) this backend is master for what it holds;
	 * 3) it's a replica and the dn supplied is the updatedn.
	 */
	if ( op->o_bd->be_add ) {
		/* do the update here */
		int repl_user = be_isupdate(op->o_bd, &op->o_ndn );
#if defined(LDAP_SYNCREPL) && !defined(SLAPD_MULTIMASTER)
		if ( !op->o_bd->syncinfo &&
						( !op->o_bd->be_update_ndn.bv_len || repl_user ))
#elif defined(LDAP_SYNCREPL) && defined(SLAPD_MULTIMASTER)
		if ( !op->o_bd->syncinfo )	/* LDAP_SYNCREPL overrides MM */
#elif !defined(LDAP_SYNCREPL) && !defined(SLAPD_MULTIMASTER)
		if ( !op->o_bd->be_update_ndn.bv_len || repl_user )
#endif
		{
			int update = op->o_bd->be_update_ndn.bv_len;
			char textbuf[SLAP_TEXT_BUFLEN];
			size_t textlen = sizeof textbuf;

			rs->sr_err = slap_mods_check( modlist, update, &rs->sr_text,
										  textbuf, textlen, NULL );

			if( rs->sr_err != LDAP_SUCCESS ) {
				send_ldap_result( op, rs );
				goto done;
			}

			if ( !repl_user ) {
				for( modtail = &modlist;
					*modtail != NULL;
					modtail = &(*modtail)->sml_next )
				{
					assert( (*modtail)->sml_op == LDAP_MOD_ADD );
					assert( (*modtail)->sml_desc != NULL );
				}
				rs->sr_err = slap_mods_opattrs( op, modlist, modtail,
					&rs->sr_text, textbuf, textlen );
				if( rs->sr_err != LDAP_SUCCESS ) {
					send_ldap_result( op, rs );
					goto done;
				}
			}

			rs->sr_err = slap_mods2entry( modlist, &e, repl_user, &rs->sr_text,
				textbuf, textlen );
			if( rs->sr_err != LDAP_SUCCESS ) {
				send_ldap_result( op, rs );
				goto done;
			}

#ifdef LDAP_SLAPI
			/*
			 * Call the preoperation plugin here, because the entry
			 * will actually contain something.
			 */
			rs->sr_err = doPreAddPluginFNs( op );
			if ( rs->sr_err != LDAP_SUCCESS ) {
				/* plugin will have sent result */
				goto done;
			}
#endif /* LDAP_SLAPI */

			op->ora_e = e;
			if ( (op->o_bd->be_add)( op, rs ) == 0 ) {
#ifdef SLAPD_MULTIMASTER
				if ( !repl_user )
#endif
				{
					replog( op );
				}
				be_entry_release_w( op, e );
				e = NULL;
			}

#if defined(LDAP_SYNCREPL) || !defined(SLAPD_MULTIMASTER)
		} else {
			BerVarray defref = NULL;
#ifdef LDAP_SLAPI
			/*
			 * SLAPI_ADD_ENTRY will be empty, but this may be acceptable
			 * on replicas (for now, it involves the minimum code intrusion).
			 */
			rs->sr_err = doPreAddPluginFNs( op );
			if ( rs->sr_err != LDAP_SUCCESS ) {
				/* plugin will have sent result */
				goto done;
			}
#endif /* LDAP_SLAPI */

#ifdef LDAP_SYNCREPL
			if ( op->o_bd->syncinfo ) {
				defref = op->o_bd->syncinfo->masteruri_bv;
			} else
#endif
			{
				defref = op->o_bd->be_update_refs
							? op->o_bd->be_update_refs : default_referral;
			}

			if ( defref != NULL ) {
				rs->sr_ref = referral_rewrite( defref,
					NULL, &e->e_name, LDAP_SCOPE_DEFAULT );
				if ( rs->sr_ref == NULL ) rs->sr_ref = defref;
				rs->sr_err = LDAP_REFERRAL;
				if (!rs->sr_ref) rs->sr_ref = default_referral;
				send_ldap_result( op, rs );

				if ( rs->sr_ref != default_referral ) ber_bvarray_free( rs->sr_ref );
			} else {
				send_ldap_error( op, rs,
						LDAP_UNWILLING_TO_PERFORM,
						"referral missing" );
			}
#endif /* SLAPD_MULTIMASTER */
		}
	} else {
#ifdef LDAP_SLAPI
	    rs->sr_err = doPreAddPluginFNs( op );
	    if ( rs->sr_err != LDAP_SUCCESS ) {
			/* plugin will have sent result */
			goto done;
		}
#endif
#ifdef NEW_LOGGING
	    LDAP_LOG( OPERATION, INFO, 
		       "do_add: conn %d	 no backend support\n", op->o_connid, 0, 0 );
#else
	    Debug( LDAP_DEBUG_ARGS, "	 do_add: no backend support\n", 0, 0, 0 );
#endif
	    send_ldap_error( op, rs, LDAP_UNWILLING_TO_PERFORM,
			"operation not supported within namingContext" );
	}

#ifdef LDAP_SLAPI
	doPostAddPluginFNs( op );
#endif /* LDAP_SLAPI */

done:
	if( modlist != NULL ) {
		slap_mods_free( modlist );
	}
	if( e != NULL ) {
		entry_free( e );
	}
	op->o_tmpfree( op->o_req_dn.bv_val, op->o_tmpmemctx );
	op->o_tmpfree( op->o_req_ndn.bv_val, op->o_tmpmemctx );

	return rs->sr_err;
}

int
slap_mods2entry(
	Modifications *mods,
	Entry **e,
	int repl_user,
	const char **text,
	char *textbuf, size_t textlen )
{
	Attribute **tail = &(*e)->e_attrs;
	assert( *tail == NULL );

	*text = textbuf;

	for( ; mods != NULL; mods = mods->sml_next ) {
		Attribute *attr;

		assert( mods->sml_op == LDAP_MOD_ADD );
		assert( mods->sml_desc != NULL );

		attr = attr_find( (*e)->e_attrs, mods->sml_desc );

		if( attr != NULL ) {
#define SLURPD_FRIENDLY
#ifdef SLURPD_FRIENDLY
			ber_len_t i,j;

			if( !repl_user ) {
				snprintf( textbuf, textlen,
					"attribute '%s' provided more than once",
					mods->sml_desc->ad_cname.bv_val );
				return LDAP_TYPE_OR_VALUE_EXISTS;
			}

			for( i=0; attr->a_vals[i].bv_val; i++ ) {
				/* count them */
			}
			for( j=0; mods->sml_values[j].bv_val; j++ ) {
				/* count them */
			}
			j++;	/* NULL */
			
			attr->a_vals = ch_realloc( attr->a_vals,
				sizeof( struct berval ) * (i+j) );

			/* should check for duplicates */

			AC_MEMCPY( &attr->a_vals[i], mods->sml_values,
				sizeof( struct berval ) * j );

			/* trim the mods array */
			ch_free( mods->sml_values );
			mods->sml_values = NULL;

			if( mods->sml_nvalues ) {
				attr->a_nvals = ch_realloc( attr->a_nvals,
					sizeof( struct berval ) * (i+j) );

				AC_MEMCPY( &attr->a_nvals[i], mods->sml_nvalues,
					sizeof( struct berval ) * j );

				/* trim the mods array */
				ch_free( mods->sml_nvalues );
				mods->sml_nvalues = NULL;

			} else {
				attr->a_nvals = attr->a_vals;
			}

			continue;
#else
			snprintf( textbuf, textlen,
				"attribute '%s' provided more than once",
				mods->sml_desc->ad_cname.bv_val );
			return LDAP_TYPE_OR_VALUE_EXISTS;
#endif
		}

		if( mods->sml_values[1].bv_val != NULL ) {
			/* check for duplicates */
			int		i, j;
			MatchingRule *mr = mods->sml_desc->ad_type->sat_equality;

			/* check if the values we're adding already exist */
			if( mr == NULL || !mr->smr_match ) {
				for ( i = 0; mods->sml_bvalues[i].bv_val != NULL; i++ ) {
					/* test asserted values against themselves */
					for( j = 0; j < i; j++ ) {
						if ( bvmatch( &mods->sml_bvalues[i],
							&mods->sml_bvalues[j] ) ) {
							/* value exists already */
							snprintf( textbuf, textlen,
								"%s: value #%d provided more than once",
								mods->sml_desc->ad_cname.bv_val, j );
							return LDAP_TYPE_OR_VALUE_EXISTS;
						}
					}
				}

			} else {
				int		rc = LDAP_SUCCESS;
				int match;

				for ( i = 0; mods->sml_values[i].bv_val != NULL; i++ ) {
					/* test asserted values against themselves */
					for( j = 0; j < i; j++ ) {
						rc = value_match( &match, mods->sml_desc, mr,
							SLAP_MR_EQUALITY | SLAP_MR_VALUE_OF_ATTRIBUTE_SYNTAX
							| SLAP_MR_ASSERTED_VALUE_NORMALIZED_MATCH
							| SLAP_MR_ATTRIBUTE_VALUE_NORMALIZED_MATCH,
							mods->sml_nvalues
								? &mods->sml_nvalues[i]
								: &mods->sml_values[i],
							mods->sml_nvalues
								? &mods->sml_nvalues[j]
								: &mods->sml_values[j],
							text );
						if ( rc == LDAP_SUCCESS && match == 0 ) {
							/* value exists already */
							snprintf( textbuf, textlen,
								"%s: value #%d provided more than once",
								mods->sml_desc->ad_cname.bv_val, j );
							return LDAP_TYPE_OR_VALUE_EXISTS;
						}
					}
				}
				if ( rc != LDAP_SUCCESS ) {
					return rc;
				}
			}
		}

		attr = ch_calloc( 1, sizeof(Attribute) );

		/* move ad to attr structure */
		attr->a_desc = mods->sml_desc;
		mods->sml_desc = NULL;

		/* move values to attr structure */
		/*	should check for duplicates */
		attr->a_vals = mods->sml_values;
		mods->sml_values = NULL;

		if ( mods->sml_nvalues ) {
			attr->a_nvals = mods->sml_nvalues;
			mods->sml_nvalues = NULL;
		} else {
			attr->a_nvals = attr->a_vals;
		}

		*tail = attr;
		tail = &attr->a_next;
	}

	return LDAP_SUCCESS;
}

#ifdef LDAP_SLAPI
static void initAddPlugin( Operation *op,
	struct berval *dn, Entry *e, int manageDSAit )
{
	slapi_x_pblock_set_operation( op->o_pb, op );
	slapi_pblock_set( op->o_pb, SLAPI_ADD_TARGET, (void *)dn->bv_val );
	slapi_pblock_set( op->o_pb, SLAPI_ADD_ENTRY, (void *)e );
	slapi_pblock_set( op->o_pb, SLAPI_MANAGEDSAIT, (void *)manageDSAit );
}

static int doPreAddPluginFNs( Operation *op )
{
	int rc;

	rc = doPluginFNs( op->o_bd, SLAPI_PLUGIN_PRE_ADD_FN, op->o_pb );
	if ( rc < 0 ) {
		/*
		 * A preoperation plugin failure will abort the
		 * entire operation.
		 */
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, INFO, "do_add: add preoperation plugin failed\n",
				0, 0, 0);
#else
		Debug(LDAP_DEBUG_TRACE, "do_add: add preoperation plugin failed.\n",
				0, 0, 0);
		if ( ( slapi_pblock_get( op->o_pb, SLAPI_RESULT_CODE, (void *)&rc ) != 0 ) ||
		     rc == LDAP_SUCCESS ) {
			rc = LDAP_OTHER;
		}
#endif
	} else {
		rc = LDAP_SUCCESS;
	}

	return rc;
}

static void doPostAddPluginFNs( Operation *op )
{
	int rc;

	rc = doPluginFNs( op->o_bd, SLAPI_PLUGIN_POST_ADD_FN, op->o_pb );
	if ( rc < 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG( OPERATION, INFO, "do_add: add postoperation plugin failed\n",
				0, 0, 0);
#else
		Debug(LDAP_DEBUG_TRACE, "do_add: add postoperation plugin failed.\n",
				0, 0, 0);
#endif
	}
}
#endif /* LDAP_SLAPI */

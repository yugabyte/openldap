/*
 * Copyright 1998-1999 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*  Portions
 *  Copyright (c) 1995 Regents of the University of Michigan.
 *  All rights reserved.
 *
 * ldap_getdxbyname - retrieve DX records from the DNS (from TXT records for now)
 */

#include "portable.h"

#ifdef LDAP_API_FEATURE_X_OPENLDAP_V2_DNS

#include <stdio.h>
#include <ac/stdlib.h>

#include <ac/ctype.h>
#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap-int.h"

static char ** decode_answer LDAP_P(( unsigned char *answer, ber_len_t len ));

#define MAX_TO_SORT	32


/*
 * ldap_getdxbyname - lookup DNS DX records for domain and return an ordered
 *	array.
 */
char **
ldap_getdxbyname( const char *domain )
{
    unsigned char	buf[ PACKETSZ ];
    char		**dxs;
    int			rc;

    Debug( LDAP_DEBUG_TRACE, "ldap_getdxbyname( %s )\n", domain, 0, 0 );

    memset( buf, 0, sizeof( buf ));

    if (( rc = res_search( domain, C_IN, T_TXT, buf, sizeof( buf ))) < 0
		|| ( dxs = decode_answer( buf, rc )) == NULL ) {
	/*
	 * punt:  return list conisting of the original domain name only
	 */
	if (( dxs = (char **)LDAP_MALLOC( 2 * sizeof( char * ))) == NULL ||
		( dxs[ 0 ] = LDAP_STRDUP( domain )) == NULL ) {
	    if ( dxs != NULL ) {
		LDAP_FREE( dxs );
	    }
	    dxs = NULL;
	} else {
	    dxs[ 1 ] = NULL;
	}
    }

    return( dxs );
}


static char **
decode_answer( unsigned char *answer, ber_len_t len )
{
    HEADER		*hp;
    char		buf[ 256 ], **dxs;
    unsigned char	*eom, *p;
    int			ancount, err, rc, type, class, dx_count, rr_len;
    int			dx_pref[ MAX_TO_SORT ];

#ifdef LDAP_DEBUG
#ifdef notdef
    if ( ldap_debug & LDAP_DEBUG_PACKETS ) {
		__p_query( answer );
    }
#endif
#endif /* LDAP_DEBUG */

    dxs = NULL;
    hp = (HEADER *)answer;
    eom = answer + len;

    if ( ntohs( hp->qdcount ) != 1 ) {
	h_errno = NO_RECOVERY;
	return( NULL );
    }

    ancount = ntohs( hp->ancount );
    if ( ancount < 1 ) {
	h_errno = NO_DATA;
	return( NULL );
    }

    /*
     * skip over the query
     */
    p = answer + HFIXEDSZ;
    if (( rc = dn_expand( answer, eom, p, buf, sizeof( buf ))) < 0 ) {
	h_errno = NO_RECOVERY;
	return( NULL );
    }
    p += ( rc + QFIXEDSZ );

    /*
     * pull out the answers we are interested in
     */
    err = dx_count = 0;
    while ( ancount > 0 && err == 0 && p < eom ) {
	if (( rc = dn_expand( answer, eom, p, buf, sizeof( buf ))) < 0 ) {
	    err = NO_RECOVERY;
	    continue;
	}
	p += rc;	/* skip over name */
	type = _getshort( p );
	p += INT16SZ;
	class = _getshort( p );
	p += INT16SZ;
	p += INT32SZ;		/* skip over TTL */
	rr_len = _getshort( p );
	p += INT16SZ;
	if ( class == C_IN && type == T_TXT ) {
	    int 	i, n, pref, txt_len;
	    char	*q, *r;

	    q = (char *)p;
	    while ( q < (char *)p + rr_len && err == 0 ) {
		if ( *q >= 3 && strncasecmp( q + 1, "dx:", 3 ) == 0 ) {
		    txt_len = *q - 3;
		    r = q + 4;
		    while ( isspace( (unsigned char) *r )) { 
			++r;
			--txt_len;
		    }
		    pref = 0;
		    while ( isdigit( (unsigned char) *r )) {
			pref *= 10;
			pref += ( *r - '0' );
			++r;
			--txt_len;
		    }
		    if ( dx_count < MAX_TO_SORT - 1 ) {
			dx_pref[ dx_count ] = pref;
		    }
		    while ( isspace( (unsigned char) *r )) { 
			++r;
			--txt_len;
		    }
			dxs = (char **)LDAP_REALLOC( dxs,
				( dx_count + 2 ) * sizeof( char * ));
		    if ( dxs == NULL || ( dxs[ dx_count ] =
				(char *)LDAP_CALLOC( 1, txt_len + 1 )) == NULL ) {
			err = NO_RECOVERY;
			continue;
		    }
		    memcpy( dxs[ dx_count ], r, txt_len );
		    dxs[ ++dx_count ] = NULL;
		}
		q += ( *q + 1 );	/* move past last TXT record */
	    }
	}
	p += rr_len;
    }

    if ( err == 0 ) {
	if ( dx_count == 0 ) {
	    err = NO_DATA;
	} else {
	    /*
	     * sort records based on associated preference value
	     */
	    int		i, j, sort_count, tmp_pref;
	    char	*tmp_dx;

	    sort_count = ( dx_count < MAX_TO_SORT ) ? dx_count : MAX_TO_SORT;
	    for ( i = 0; i < sort_count; ++i ) {
		for ( j = i + 1; j < sort_count; ++j ) {
		    if ( dx_pref[ i ] > dx_pref[ j ] ) {
			tmp_pref = dx_pref[ i ];
			dx_pref[ i ] = dx_pref[ j ];
			dx_pref[ j ] = tmp_pref;
			tmp_dx = dxs[ i ];
			dxs[ i ] = dxs[ j ];
			dxs[ j ] = tmp_dx;
		    }
		}
	    }
	}
    }

    h_errno = err;
    return( dxs );
}

#endif /* LDAP_API_FEATURE_X_OPENLDAP_V2_DNS */

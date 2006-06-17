/*
 * card-setcos.c: Support for PKI cards by Setec
 *
 * Copyright (C) 2001, 2002  Juha Yrj�l� <juha.yrjola@iki.fi>
 * Copyright (C) 2005  Antti Tapaninen <aet@cc.hut.fi>
 * Copyright (C) 2005  Zetes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "internal.h"
#include "cardctl.h"
#include <stdlib.h>
#include <string.h>

#include <opensc/asn1.h>

static struct sc_atr_table setcos_atrs[] = {
	/* some Nokia branded SC */
	{ "3B:1F:11:00:67:80:42:46:49:53:45:10:52:66:FF:81:90:00", NULL, NULL, SC_CARD_TYPE_SETCOS_GENERIC, 0, NULL },
	/* RSA SecurID 3100 */
	{ "3B:9F:94:40:1E:00:67:16:43:46:49:53:45:10:52:66:FF:81:90:00", NULL, NULL, SC_CARD_TYPE_SETCOS_PKI, 0, NULL },

	/* FINEID 1016 (SetCOS 4.3.1B3/PKCS#15, VRK) */
	{ "3b:9f:94:40:1e:00:67:00:43:46:49:53:45:10:52:66:ff:81:90:00", "ff:ff:ff:ff:ff:ff:ff:00:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff", NULL, SC_CARD_TYPE_SETCOS_FINEID, SC_CARD_FLAG_RNG, NULL },
	/* FINEID 2032 (EIDApplet/7816-15, VRK test) */
	{ "3b:6b:00:ff:80:62:00:a2:56:46:69:6e:45:49:44", "ff:ff:00:ff:ff:ff:00:ff:ff:ff:ff:ff:ff:ff:ff", NULL, SC_CARD_TYPE_SETCOS_FINEID_V2, 0, NULL },
	/* FINEID 2132 (EIDApplet/7816-15, 3rdparty test) */
	{ "3b:64:00:ff:80:62:00:a2", "ff:ff:00:ff:ff:ff:00:ff", NULL, SC_CARD_TYPE_SETCOS_FINEID_V2, 0, NULL },
	/* FINEID 2064 (EIDApplet/7816-15, VRK) */
	{ "3b:7b:00:00:00:80:62:00:51:56:46:69:6e:45:49:44", "ff:ff:00:ff:ff:ff:ff:f0:ff:ff:ff:ff:ff:ff:ff:ff", NULL, SC_CARD_TYPE_SETCOS_FINEID_V2, 0, NULL },
	/* FINEID 2164 (EIDApplet/7816-15, 3rdparty) */
	{ "3b:64:00:00:80:62:00:51", "ff:ff:ff:ff:ff:ff:f0:ff", NULL, SC_CARD_TYPE_SETCOS_FINEID_V2, 0, NULL },
	/* FINEID 2264 (EIDApplet/7816-15, OPK/EMV/AVANT) */
	{ "3b:6e:00:00:00:62:00:00:57:41:56:41:4e:54:10:81:90:00", NULL, NULL, SC_CARD_TYPE_SETCOS_FINEID_V2, 0, NULL },
	{ "3b:7b:94:00:00:80:62:11:51:56:46:69:6e:45:49:44", NULL, NULL, SC_CARD_TYPE_SETCOS_FINEID_V2, 0, NULL },
	/* Swedish NIDEL card */
	{ "3b:9f:94:80:1f:c3:00:68:10:44:05:01:46:49:53:45:31:c8:07:90:00:18", NULL, NULL, SC_CARD_TYPE_SETCOS_NIDEL, 0, NULL },
	/* Setcos 4.4.1 */
	{ "3b:9f:94:80:1f:c3:00:68:11:44:05:01:46:49:53:45:31:c8:00:00:00:00", "ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:00:00:00:00", NULL, SC_CARD_TYPE_SETCOS_44, 0, NULL },
	{ NULL, NULL, NULL, 0, 0, NULL }
};

/* Setcos 4.4 Life Cycle Status Integer  */
#define SETEC_LCSI_CREATE      0x01
#define SETEC_LCSI_INIT        0x03
#define SETEC_LCSI_ACTIVATED   0x07
#define SETEC_LCSI_DEACTIVATE  0x06
#define SETEC_LCSI_TEMINATE    0x0F /* MF only  */

static struct sc_card_operations setcos_ops;
static struct sc_card_driver setcos_drv = {
	"Setec cards",
	"setcos",
	&setcos_ops,
	NULL, 0, NULL
};

static int setcos_finish(sc_card_t *card)
{
	return 0;
}

static int match_hist_bytes(sc_card_t *card, const char *str, size_t len)
{
	const char *src = (const char *) card->slot->atr_info.hist_bytes;
	size_t srclen = card->slot->atr_info.hist_bytes_len;
	size_t offset = 0;

	if (len == 0)
		len = strlen(str);
	if (srclen < len)
		return 0;
	while (srclen - offset > len) {
		if (memcmp(src + offset, str, len) == 0) {
			return 1;
		}
		offset++;
	}
	return 0;
}

static int setcos_match_card(sc_card_t *card)
{
	int i;

	i = _sc_match_atr(card, setcos_atrs, &card->type);
	if (i < 0) {
		/* Unknown card, but has the FinEID application for sure */
		if (match_hist_bytes(card, "FinEID", 0)) {
			card->type = SC_CARD_TYPE_SETCOS_FINEID_V2;
			return 1;
		}
		if (match_hist_bytes(card, "FISE", 0)) {
			card->type = SC_CARD_TYPE_SETCOS_GENERIC;
			return 1;
		}
		return 0;
	}
	card->flags = setcos_atrs[i].flags;
	return 1;
}

static int select_pkcs15_app(sc_card_t * card)
{
	sc_path_t app;
	int r;

	/* Regular PKCS#15 AID */
	sc_format_path("A000000063504B43532D3135", &app);
	app.type = SC_PATH_TYPE_DF_NAME;
	sc_ctx_suppress_errors_on(card->ctx);
	r = sc_select_file(card, &app, NULL);
	sc_ctx_suppress_errors_off(card->ctx);
	return r;
}

static int setcos_init(sc_card_t *card)
{
	card->name = "SetCOS";

	/* Handle unknown or forced cards */
	if (card->type < 0) {
		card->type = SC_CARD_TYPE_SETCOS_GENERIC;
#if 0
		/* Hmm. For now, assume it's a bank card with FinEID application */
		if (match_hist_bytes(card, "AVANT", 0))
			card->type = SC_CARD_TYPE_SETCOS_FINEID_V2;
#endif
	}

	switch (card->type) {
	case SC_CARD_TYPE_SETCOS_FINEID:
	case SC_CARD_TYPE_SETCOS_FINEID_V2:
	case SC_CARD_TYPE_SETCOS_NIDEL:
		card->cla = 0x00;
		select_pkcs15_app(card);
		if (card->flags & SC_CARD_FLAG_RNG)
			card->caps |= SC_CARD_CAP_RNG;
		break;
	case SC_CARD_TYPE_SETCOS_44:
		card->cla = 0x00;
		card->caps |= SC_CARD_CAP_USE_FCI_AC;
		card->caps |= SC_CARD_CAP_RNG;
		card->caps |= SC_CARD_FLAG_ONBOARD_KEY_GEN;
		break;
	default:
		/* XXX: Get SetCOS version */
		card->cla = 0x80;	/* SetCOS 4.3.x */
		/* State that we have an RNG */
		card->caps |= SC_CARD_CAP_RNG;
		break;
	}

	switch (card->type) {
	case SC_CARD_TYPE_SETCOS_PKI:
	case SC_CARD_TYPE_SETCOS_FINEID:
	case SC_CARD_TYPE_SETCOS_FINEID_V2:
		{
			unsigned long flags;

			flags = SC_ALGORITHM_RSA_RAW | SC_ALGORITHM_RSA_PAD_PKCS1;
			flags |= SC_ALGORITHM_RSA_HASH_NONE | SC_ALGORITHM_RSA_HASH_SHA1;

			_sc_card_add_rsa_alg(card, 1024, flags, 0);
		}
		break;
	case SC_CARD_TYPE_SETCOS_44:
	case SC_CARD_TYPE_SETCOS_NIDEL:
		{
			unsigned long flags;

			flags = SC_ALGORITHM_RSA_RAW | SC_ALGORITHM_RSA_PAD_PKCS1;
			flags |= SC_ALGORITHM_RSA_HASH_NONE | SC_ALGORITHM_RSA_HASH_SHA1;
			flags |= SC_ALGORITHM_ONBOARD_KEY_GEN;

			_sc_card_add_rsa_alg(card, 512, flags, 0);
			_sc_card_add_rsa_alg(card, 768, flags, 0);
			_sc_card_add_rsa_alg(card, 1024, flags, 0);
		}
		break;
	}
	return 0;
}

static const struct sc_card_operations *iso_ops = NULL;

static int setcos_construct_fci_44(sc_card_t *card, const sc_file_t *file, u8 *out, size_t *outlen)
{
	u8 *p = out;
	u8 buf[64];
	const u8 *pin_key_info;

	/* Command */
	*p++ = 0x6F;
	p++;

	/* Length */
	buf[0] = (file->size >> 8) & 0xFF;
	buf[1] = file->size & 0xFF;
	sc_asn1_put_tag(0x81, buf, 2, p, 16, &p);

	/* Type */
	if (file->type_attr_len) {
		memcpy(buf, file->type_attr, file->type_attr_len);
		sc_asn1_put_tag(0x82, buf, file->type_attr_len, p, 16, &p);
	} else {
		u8	bLen = 1;

		buf[0] = file->shareable ? 0x40 : 0;
		switch (file->type) {
		case SC_FILE_TYPE_INTERNAL_EF:				/* RSA keyfile */
			buf[0] = 0x11;				
			break;
		case SC_FILE_TYPE_WORKING_EF:
			if (file->ef_structure == 0x22) {		/* pin-file */
				/* ISF key-file, Setcos V4.4 specific */
				bLen = 5;
				buf[0] = 0x0A;				/* EF linear fixed EF for ISF keys */
				buf[1] = 0x41;				/* fixed */
				buf[2] = file->record_length >> 8;	/* 2 byte record length  */
				buf[3] = file->record_length & 0xFF;
				buf[4] = file->size / file->record_length; /* record count */
			} else {
				buf[0] |= file->ef_structure & 7;	/* set file-type, only for EF, not for DF objects  */
			}
			break;
		case SC_FILE_TYPE_DF:	
			buf[0] = 0x38;
			break;
		default:
			return SC_ERROR_NOT_SUPPORTED;
		}
		sc_asn1_put_tag(0x82, buf, bLen, p, 16, &p);
	}

	/* File-id */
	buf[0] = (file->id >> 8) & 0xFF;
	buf[1] = file->id & 0xFF;
	sc_asn1_put_tag(0x83, buf, 2, p, 16, &p);

	/* file state */
	if (file->prop_attr_len) {
		memcpy(buf, file->prop_attr, file->prop_attr_len);
		sc_asn1_put_tag(0x8A, buf, file->prop_attr_len, p, 18, &p);
	}

	/* Access control */
	memcpy(buf, file->sec_attr, file->sec_attr_len);
	sc_asn1_put_tag(0x86, buf, file->sec_attr_len, p, 18, &p);

	switch (file->type) {
	case SC_FILE_TYPE_DF:
		if (file->name[0] != 0)
			sc_asn1_put_tag(0x84, (u8 *) file->name, file->namelen, p, 18, &p);
		else { /* Name required -> take the FID */
			buf[0] = (file->id >> 8) & 0xFF;
			buf[1] = file->id & 0xFF;
			sc_asn1_put_tag(0x84, buf, 2, p, 16, &p);
		}

		/* Pin/key info: define 4 pins, no keys */
		if(file->path.len == 2)
			pin_key_info = (const u8*)"\xC1\x04\x81\x82\x83\x84\xC2\x00";	/* root-MF: use local pin-file */
		else
			pin_key_info = (const u8 *)"\xC1\x04\x01\x02\x03\x04\xC2\x00";	/* sub-DF: use parent pin-file in root-MF */
		sc_asn1_put_tag(0xA5, pin_key_info, 8, p, 18, &p);
		break;

	case SC_FILE_TYPE_INTERNAL_EF:					/* RSA keyfiles  */
		/* Get pin-number */	
		/* Define AC for RSA-files */
		break;
	}

	/* Length   */
	out[1] = p - out - 2;

	*outlen = p - out;
	return 0;
}

static int setcos_construct_fci(sc_card_t *card, const sc_file_t *file, u8 *out, size_t *outlen)
{
	if (card->type == SC_CARD_TYPE_SETCOS_44 || 
	    card->type == SC_CARD_TYPE_SETCOS_NIDEL)
		return setcos_construct_fci_44(card, file, out, outlen);
	else
		return iso_ops->construct_fci(card, file, out, outlen);
}

static u8 acl_to_byte(const sc_acl_entry_t *e)
{
	switch (e->method) {
	case SC_AC_NONE:
		return 0x00;
	case SC_AC_CHV:
		switch (e->key_ref) {
		case 1:
			return 0x01;
			break;
		case 2:
			return 0x02;
			break;
		default:
			return 0x00;
		}
		break;
	case SC_AC_TERM:
		return 0x04;
	case SC_AC_NEVER:
		return 0x0F;
	}
	return 0x00;
}

static unsigned int acl_to_byte_44(const struct sc_acl_entry *e, u8* p_bNumber)
{
	/* Handle special fixed values */
	if (e == (sc_acl_entry_t *) 1)           /* SC_AC_NEVER */
		return SC_AC_NEVER;
	else if ((e == (sc_acl_entry_t *) 2) ||  /* SC_AC_NONE */
	         (e == (sc_acl_entry_t *) 3) ||  /* SC_AC_UNKNOWN */
	         (e == (sc_acl_entry_t *) 0))
		return SC_AC_NONE;

	/* Handle standard values */
	*p_bNumber = e->key_ref;
	return(e->method);
}

/* If pin is present in the pins list, return it's index.
 * If it's not yet present, add it to the list and return the index. */
static int setcos_pin_index_44(int *pins, int len, int pin)
{
	int i;
	for (i = 0; i < len; i++) {
		if (pins[i] == pin)
			return i;
		if (pins[i] == -1) {
			pins[i] = pin;
			return i;
		}
	}
	assert(i != len); /* Too much PINs, shouldn't happen */
	return 0;
}

/* The ACs are allways for the SETEC_LCSI_ACTIVATED state, even if
 * we have to create the file in the SC_FILE_STATUS_INITIALISATION state. */
static int setcos_create_file_44(sc_card_t *card, sc_file_t *file)
{
	const u8 bFileStatus = file->status == SC_FILE_STATUS_CREATION ?
		SETEC_LCSI_CREATE : SETEC_LCSI_ACTIVATED;
	u8 bCommands_always = 0;
	int pins[] = {-1, -1, -1, -1, -1, -1, -1};
	u8 bCommands_pin[sizeof(pins)/sizeof(pins[0])]; /* both 7 entries big */
	u8 bCommands_key = 0;
	u8 bNumber = 0;
	u8 bPinNumber = 0;
	u8 bKeyNumber = 0;
	unsigned int bMethod = 0;

	/* -1 means RFU */
	const int df_idx[8] = {  /* byte 1 = OpenSC type of AC Bit0,  byte 2 = OpenSC type of AC Bit1 ...*/
		SC_AC_OP_DELETE, SC_AC_OP_CREATE, SC_AC_OP_CREATE,
		SC_AC_OP_INVALIDATE, SC_AC_OP_REHABILITATE,
		SC_AC_OP_LOCK, SC_AC_OP_DELETE, -1};
	const int ef_idx[8] = {  /* note: SC_AC_OP_SELECT to be ignored, actually RFU */
		SC_AC_OP_READ, SC_AC_OP_UPDATE, SC_AC_OP_WRITE,
		SC_AC_OP_INVALIDATE, SC_AC_OP_REHABILITATE,
		-1, SC_AC_OP_ERASE, -1};
	const int efi_idx[8] = {  /* internal EF used for RSA keys */
		SC_AC_OP_READ, SC_AC_OP_ERASE, SC_AC_OP_UPDATE,
		SC_AC_OP_INVALIDATE, SC_AC_OP_REHABILITATE,
		-1, SC_AC_OP_ERASE, -1};

	/* Set file creation status  */
	sc_file_set_prop_attr(file, &bFileStatus, 1);

	/* Build ACI from local structure = get AC for each operation group */
	if (file->sec_attr_len == 0) {
		const int* p_idx;
		int	       i;
		int	       len = 0;
		u8         bBuf[32];

		/* Get specific operation groups for specified file-type */
		switch (file->type){
		case SC_FILE_TYPE_DF:           /* DF */
			p_idx = df_idx;
			break;
		case SC_FILE_TYPE_INTERNAL_EF:  /* EF for RSA keys */
			p_idx = efi_idx;
			break;
		default:                        /* SC_FILE_TYPE_WORKING_EF */
			p_idx = ef_idx;
			break;
		}

		/* Get enabled commands + required Keys/Pins  */
		memset(bCommands_pin, 0, sizeof(bCommands_pin));
		for (i = 7; i >= 0; i--) {  /* for each AC Setcos operation */
			bCommands_always <<= 1;
			bCommands_key <<= 1;

			if (p_idx[i] == -1)  /* -1 means that bit is RFU -> set to 0 */
				continue;

			bMethod = acl_to_byte_44(file->acl[ p_idx[i] ], &bNumber);
			/* Convert to OpenSc-index, convert to pin/key number */
			switch(bMethod){
			case SC_AC_NONE:			/* always allowed */
				bCommands_always |= 1;
				break;
			case SC_AC_CHV:				/* pin */
				if (bNumber == 0 || bNumber > 7) {
					sc_error(card->ctx, "SetCOS 4.4 PIN refs can only be 1..7\n");
					return SC_ERROR_INVALID_ARGUMENTS;
				}
				bPinNumber = bNumber;
				bCommands_pin[setcos_pin_index_44(pins, sizeof(pins), (int) bNumber)] |= 1 << i;
				break;
			case SC_AC_TERM:			/* key */
				bKeyNumber = bNumber;	/* There should be only 1 key */
				bCommands_key |= 1;
				break;
			}
		}

		/* Add the commands that are allways allowed */
		if (bCommands_always) {
			bBuf[len++] = 1;
			bBuf[len++] = bCommands_always;
		}
		/* Add commands that require pins */
		for (i = 0; i < (int)sizeof(bCommands_pin) && pins[i] != -1; i++) {
			bBuf[len++] = 2;
			bBuf[len++] = bCommands_pin[i];
			bBuf[len++] = pins[i] & 0x07;  /* pin ref */
		}
		/* Add ommands that require the key */
		if (bCommands_key) {
			bBuf[len++] = 2 | 0x20;			/* indicate keyNumber present */
			bBuf[len++] = bCommands_key;
			bBuf[len++] = bKeyNumber;
		}
		/* RSA signing/decryption requires AC adaptive coding,  can't be put
		   in AC simple coding. Only implemented for pins, not for a key. */
		bPinNumber = 0;
		bKeyNumber = 0;
		if ( (file->type == SC_FILE_TYPE_INTERNAL_EF) &&
		     (acl_to_byte_44(file->acl[SC_AC_OP_CRYPTO], &bNumber) == SC_AC_CHV) ) {
			bBuf[len++] = 0x83;
			bBuf[len++] = 0x01;
			bBuf[len++] = 0x2A;  /* INS byte for the sign/decrypt APDU */
			bBuf[len++] = bNumber & 0x07;  /* pin ref */
		}

		sc_file_set_sec_attr(file, bBuf, len);
	}

	return iso_ops->create_file(card, file);
}

static int setcos_create_file(sc_card_t *card, sc_file_t *file)
{
	if (card->type == SC_CARD_TYPE_SETCOS_44)
		return setcos_create_file_44(card, file);

	if (file->prop_attr_len == 0)
		sc_file_set_prop_attr(file, (const u8 *) "\x03\x00\x00", 3);
	if (file->sec_attr_len == 0) {
		int idx[6], i;
		u8 buf[6];

		if (file->type == SC_FILE_TYPE_DF) {
			const int df_idx[6] = {
				SC_AC_OP_SELECT, SC_AC_OP_LOCK, SC_AC_OP_DELETE,
				SC_AC_OP_CREATE, SC_AC_OP_REHABILITATE,
				SC_AC_OP_INVALIDATE
			};
			for (i = 0; i < 6; i++)
				idx[i] = df_idx[i];
		} else {
			const int ef_idx[6] = {
				SC_AC_OP_READ, SC_AC_OP_UPDATE, SC_AC_OP_WRITE,
				SC_AC_OP_ERASE, SC_AC_OP_REHABILITATE,
				SC_AC_OP_INVALIDATE
			};
			for (i = 0; i < 6; i++)
				idx[i] = ef_idx[i];
		}
		for (i = 0; i < 6; i++) {
			const struct sc_acl_entry *entry;
			entry = sc_file_get_acl_entry(file, idx[i]);
			buf[i] = acl_to_byte(entry);
		}

		sc_file_set_sec_attr(file, buf, 6);
	}

	return iso_ops->create_file(card, file);
}

static int setcos_set_security_env2(sc_card_t *card,
				    const sc_security_env_t *env, int se_num)
{
	sc_apdu_t apdu;
	u8 sbuf[SC_MAX_APDU_BUFFER_SIZE];
	u8 *p;
	int r, locked = 0;

	assert(card != NULL && env != NULL);

	if (card->type == SC_CARD_TYPE_SETCOS_44 ||
	    card->type == SC_CARD_TYPE_SETCOS_NIDEL) {
		if (env->flags & SC_SEC_ENV_KEY_REF_ASYMMETRIC) {
			sc_error(card->ctx, "asymmetric keyref not supported.\n");
			return SC_ERROR_NOT_SUPPORTED;
		}
		if (se_num > 0) {
			sc_error(card->ctx, "restore security environment not supported.\n");
			return SC_ERROR_NOT_SUPPORTED;
		}
	}

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0, 0);
	switch (env->operation) {
	case SC_SEC_OPERATION_DECIPHER:
		/* Should be 0x81 */
		apdu.p1 = 0x41;
		apdu.p2 = 0xB8;
		break;
	case SC_SEC_OPERATION_SIGN:
		/* Should be 0x41 */
		apdu.p1 = ((card->type == SC_CARD_TYPE_SETCOS_FINEID_V2) ||
		           (card->type == SC_CARD_TYPE_SETCOS_44) ||
			   (card->type == SC_CARD_TYPE_SETCOS_NIDEL)) ? 0x41 : 0x81;
		apdu.p2 = 0xB6;
		break;
	default:
		return SC_ERROR_INVALID_ARGUMENTS;
	}
	apdu.le = 0;
	p = sbuf;
	if (env->flags & SC_SEC_ENV_ALG_REF_PRESENT) {
		*p++ = 0x80;	/* algorithm reference */
		*p++ = 0x01;
		*p++ = env->algorithm_ref & 0xFF;
	}
	if (env->flags & SC_SEC_ENV_FILE_REF_PRESENT) {
		*p++ = 0x81;
		*p++ = env->file_ref.len;
		memcpy(p, env->file_ref.value, env->file_ref.len);
		p += env->file_ref.len;
	}
	if (env->flags & SC_SEC_ENV_KEY_REF_PRESENT) {
		if (env->flags & SC_SEC_ENV_KEY_REF_ASYMMETRIC)
			*p++ = 0x83;
		else
			*p++ = 0x84;
		*p++ = env->key_ref_len;
		memcpy(p, env->key_ref, env->key_ref_len);
		p += env->key_ref_len;
	}
	r = p - sbuf;
	apdu.lc = r;
	apdu.datalen = r;
	apdu.data = sbuf;
	apdu.resplen = 0;
	if (se_num > 0) {
		r = sc_lock(card);
		SC_TEST_RET(card->ctx, r, "sc_lock() failed");
		locked = 1;
	}
	if (apdu.datalen != 0) {
		r = sc_transmit_apdu(card, &apdu);
		if (r) {
			sc_perror(card->ctx, r, "APDU transmit failed");
			goto err;
		}
		r = sc_check_sw(card, apdu.sw1, apdu.sw2);
		if (r) {
			sc_perror(card->ctx, r, "Card returned error");
			goto err;
		}
	}
	if (se_num <= 0)
		return 0;
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x22, 0xF2, se_num);
	r = sc_transmit_apdu(card, &apdu);
	sc_unlock(card);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
err:
	if (locked)
		sc_unlock(card);
	return r;
}

static int setcos_set_security_env(sc_card_t *card,
				   const sc_security_env_t *env, int se_num)
{
	if (env->flags & SC_SEC_ENV_ALG_PRESENT) {
		sc_security_env_t tmp;

		tmp = *env;
		tmp.flags &= ~SC_SEC_ENV_ALG_PRESENT;
		tmp.flags |= SC_SEC_ENV_ALG_REF_PRESENT;
		if (tmp.algorithm != SC_ALGORITHM_RSA) {
			sc_error(card->ctx, "Only RSA algorithm supported.\n");
			return SC_ERROR_NOT_SUPPORTED;
		}
		switch (card->type) {
		case SC_CARD_TYPE_SETCOS_PKI:
		case SC_CARD_TYPE_SETCOS_FINEID:
		case SC_CARD_TYPE_SETCOS_FINEID_V2:
		case SC_CARD_TYPE_SETCOS_NIDEL:
		case SC_CARD_TYPE_SETCOS_44:
			break;
		default:
			sc_error(card->ctx, "Card does not support RSA.\n");
			return SC_ERROR_NOT_SUPPORTED;
			break;
		}
		tmp.algorithm_ref = 0x00;
		/* potential FIXME: return an error, if an unsupported
		 * pad or hash was requested, although this shouldn't happen.
		 */
		if (env->algorithm_flags & SC_ALGORITHM_RSA_PAD_PKCS1)
			tmp.algorithm_ref = 0x02;
		if (tmp.algorithm_flags & SC_ALGORITHM_RSA_HASH_SHA1)
			tmp.algorithm_ref |= 0x10;
		return setcos_set_security_env2(card, &tmp, se_num);
	}
	return setcos_set_security_env2(card, env, se_num);
}

static void add_acl_entry(sc_file_t *file, int op, u8 byte)
{
	unsigned int method, key_ref = SC_AC_KEY_REF_NONE;

	switch (byte >> 4) {
	case 0:
		method = SC_AC_NONE;
		break;
	case 1:
		method = SC_AC_CHV;
		key_ref = 1;
		break;
	case 2:
		method = SC_AC_CHV;
		key_ref = 2;
		break;
	case 4:
		method = SC_AC_TERM;
		break;
	case 15:
		method = SC_AC_NEVER;
		break;
	default:
		method = SC_AC_UNKNOWN;
		break;
	}
	sc_file_add_acl_entry(file, op, method, key_ref);
}

static void parse_sec_attr(sc_file_t *file, const u8 * buf, size_t len)
{
	int i;
	int idx[6];

	if (len < 6)
		return;
	if (file->type == SC_FILE_TYPE_DF) {
		const int df_idx[6] = {
			SC_AC_OP_SELECT, SC_AC_OP_LOCK, SC_AC_OP_DELETE,
			SC_AC_OP_CREATE, SC_AC_OP_REHABILITATE,
			SC_AC_OP_INVALIDATE
		};
		for (i = 0; i < 6; i++)
			idx[i] = df_idx[i];
	} else {
		const int ef_idx[6] = {
			SC_AC_OP_READ, SC_AC_OP_UPDATE, SC_AC_OP_WRITE,
			SC_AC_OP_ERASE, SC_AC_OP_REHABILITATE,
			SC_AC_OP_INVALIDATE
		};
		for (i = 0; i < 6; i++)
			idx[i] = ef_idx[i];
	}
	for (i = 0; i < 6; i++)
		add_acl_entry(file, idx[i], buf[i]);
}

static void parse_sec_attr_44(sc_file_t *file, const u8 *buf, size_t len)
{
	/* OpenSc Operation values for each command operation-type */
	const int df_idx[8] = {	 /* byte 1 = OpenSC type of AC Bit0,  byte 2 = OpenSC type of AC Bit1 ...*/
		SC_AC_OP_DELETE, SC_AC_OP_CREATE, SC_AC_OP_CREATE,
		SC_AC_OP_INVALIDATE, SC_AC_OP_REHABILITATE,
		SC_AC_OP_LOCK, SC_AC_OP_DELETE, -1};
	const int ef_idx[8] = {
		SC_AC_OP_READ, SC_AC_OP_UPDATE, SC_AC_OP_WRITE,
		SC_AC_OP_INVALIDATE, SC_AC_OP_REHABILITATE,
		-1, SC_AC_OP_ERASE, -1};
	const int efi_idx[8] = { /* internal EF used for RSA keys */
		SC_AC_OP_READ, SC_AC_OP_ERASE, SC_AC_OP_UPDATE,
		SC_AC_OP_INVALIDATE, SC_AC_OP_REHABILITATE,
		-1, SC_AC_OP_ERASE, -1};

	u8		bValue;
	int		i;
	int		iKeyRef = 0;
	int		iMethod;
	int		iPinCount;
	int		iOffset = 0;
	int		iOperation;
	const int*	p_idx;

	/* Check all sub-AC definitions whitin the total AC */
	while (len > 1) {				/* minimum length = 2 */
		int	iACLen   = buf[iOffset] & 0x0F;

		iPinCount = -1;			/* default no pin required */
		iMethod = SC_AC_NONE;		/* default no authentication required */

		if (buf[iOffset] & 0X80) { /* AC in adaptive coding */
			/* Evaluates only the command-byte, not the optional P1/P2/Option bytes */
			int	iParmLen = 1;			/* command-byte is always present */
			int	iKeyLen  = 0;			/* Encryption key is optional */

			if (buf[iOffset]   & 0x20) iKeyLen++;
			if (buf[iOffset+1] & 0x40) iParmLen++;
			if (buf[iOffset+1] & 0x20) iParmLen++;
			if (buf[iOffset+1] & 0x10) iParmLen++;
			if (buf[iOffset+1] & 0x08) iParmLen++;

			/* Get KeyNumber if available */
			if(iKeyLen) {
				int iSC = buf[iOffset+iACLen];

				switch( (iSC>>5) & 0x03 ){
				case 0:
					iMethod = SC_AC_TERM;		/* key authentication */
					break;
				case 1:
					iMethod = SC_AC_AUT;		/* key authentication  */
					break;
				case 2:
				case 3:
					iMethod = SC_AC_PRO;		/* secure messaging */
					break;
				}
				iKeyRef = iSC & 0x1F;			/* get key number */
			}

			/* Get PinNumber if available */
			if (iACLen > (1+iParmLen+iKeyLen)) {  /* check via total length if pin is present */
				iKeyRef = buf[iOffset+1+1+iParmLen];  /* PTL + AM-header + parameter-bytes */
				iMethod = SC_AC_CHV;
			}

			/* Convert SETCOS command to OpenSC command group */
			switch(buf[iOffset+2]){
			case 0x2A:			/* crypto operation */
				iOperation = SC_AC_OP_CRYPTO;
				break;
			case 0x46:			/* key-generation operation */
				iOperation = SC_AC_OP_UPDATE;
				break;
			default:
				iOperation = SC_AC_OP_SELECT;
				break;
			}
			sc_file_add_acl_entry(file, iOperation, iMethod, iKeyRef);
		}
		else { /* AC in simple coding */
			   /* Initial AC is treated as an operational AC */

			/* Get specific Cmd groups for specified file-type */
			switch (file->type) {
			case SC_FILE_TYPE_DF:            /* DF */
				p_idx = df_idx;
				break;
			case SC_FILE_TYPE_INTERNAL_EF:   /* EF for RSA keys */
				p_idx = efi_idx;
				break;
			default:                         /* EF */
				p_idx = ef_idx;
				break;
			}

			/* Encryption key present ? */
			iPinCount = iACLen - 1;		

			if (buf[iOffset] & 0x20) {
				int iSC = buf[iOffset + iACLen];

				switch( (iSC>>5) & 0x03 ) {
				case 0:
					iMethod = SC_AC_TERM;		/* key authentication */
					break;
				case 1:
					iMethod = SC_AC_AUT;		/* key authentication  */
					break;
				case 2:
				case 3:
					iMethod = SC_AC_PRO;		/* secure messaging */
					break;
				}
				iKeyRef = iSC & 0x1F;			/* get key number */

				iPinCount--;				/* one byte used for keyReference  */
			}

			/* Pin present ? */
			if ( iPinCount > 0 ) {
				iKeyRef = buf[iOffset + 2];	/* pin ref */
				iMethod = SC_AC_CHV;
			}

			/* Add AC for each command-operationType into OpenSc structure */
			bValue = buf[iOffset + 1];
			for (i = 0; i < 8; i++) {
				if((bValue & 1) && (p_idx[i] >= 0))
					sc_file_add_acl_entry(file, p_idx[i], iMethod, iKeyRef);
				bValue >>= 1;
			}
		}
		/* Current field treated, get next AC sub-field */
		iOffset += iACLen +1;		/* AC + PTL-byte */
		len     -= iACLen +1;
	}
}

static int setcos_select_file(sc_card_t *card,
			      const sc_path_t *in_path, sc_file_t **file)
{
	int r;

	r = iso_ops->select_file(card, in_path, file);
	if (r)
		return r;
	if (file != NULL) {
		if (card->type == SC_CARD_TYPE_SETCOS_44 ||
		    card->type == SC_CARD_TYPE_SETCOS_NIDEL)
			parse_sec_attr_44(*file, (*file)->sec_attr, (*file)->sec_attr_len);
		else
			parse_sec_attr(*file, (*file)->sec_attr, (*file)->sec_attr_len);
	}
	return 0;
}

static int setcos_list_files(sc_card_t *card, u8 * buf, size_t buflen)
{
	sc_apdu_t apdu;
	int r;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0xAA, 0, 0);
	if (card->type == SC_CARD_TYPE_SETCOS_44 || 
	    card->type == SC_CARD_TYPE_SETCOS_NIDEL)
		apdu.cla = 0x80;
	apdu.resp = buf;
	apdu.resplen = buflen;
	apdu.le = buflen > 256 ? 256 : buflen;
	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");
	if (card->type == SC_CARD_TYPE_SETCOS_44 && apdu.sw1 == 0x6A && apdu.sw2 == 0x82)
		return 0; /* no files found */
	if (apdu.resplen == 0)
		return sc_check_sw(card, apdu.sw1, apdu.sw2);
	return apdu.resplen;
}

static int setcos_process_fci(sc_card_t *card, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	int r = iso_ops->process_fci(card, file, buf, buflen);

	/* SetCOS 4.4: RSA key file is an internal EF but it's
	 * file descriptor doesn't seem to follow ISO7816. */
	if (r >= 0 && card->type == SC_CARD_TYPE_SETCOS_44) {
		const u8 *tag;
		size_t taglen = 1;
		tag = (u8 *) sc_asn1_find_tag(card->ctx, buf, buflen, 0x82, &taglen);
		if (tag != NULL && taglen == 1 && *tag == 0x11)
			file->type = SC_FILE_TYPE_INTERNAL_EF;
	}

	return r;
}

/* Write internal data, e.g. add default pin-records to pin-file */
static int setcos_putdata(struct sc_card *card, struct sc_cardctl_setcos_data_obj* data_obj)
{
	int				r;
	struct sc_apdu			apdu;

	SC_FUNC_CALLED(card->ctx, 1);

	memset(&apdu, 0, sizeof(apdu));
	apdu.cse     = SC_APDU_CASE_3_SHORT;
	apdu.cla     = 0x00;
	apdu.ins     = 0xDA;
	apdu.p1      = data_obj->P1;
	apdu.p2      = data_obj->P2;
	apdu.lc      = data_obj->DataLen;
	apdu.datalen = data_obj->DataLen;
	apdu.data    = data_obj->Data;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "PUT_DATA returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

/* Read internal data, e.g. get RSA public key */
static int setcos_getdata(struct sc_card *card, struct sc_cardctl_setcos_data_obj* data_obj)
{
	int				r;
	struct sc_apdu			apdu;

	SC_FUNC_CALLED(card->ctx, 1);

	memset(&apdu, 0, sizeof(apdu));
	apdu.cse     = SC_APDU_CASE_2_SHORT;
	apdu.cla     = 0x00;
	apdu.ins     = 0xCA;			/* GET DATA */
	apdu.p1      = data_obj->P1;
	apdu.p2      = data_obj->P2;
	apdu.lc      = 0;
	apdu.datalen = 0;
	apdu.data    = data_obj->Data;

	apdu.le      = 256;
	apdu.resp    = data_obj->Data;
	apdu.resplen = data_obj->DataLen;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "GET_DATA returned error");

	if (apdu.resplen > data_obj->DataLen)
		r = SC_ERROR_WRONG_LENGTH;
	else
		data_obj->DataLen = apdu.resplen;

	SC_FUNC_RETURN(card->ctx, 1, r);
}

/* Generate or store a key */
static int setcos_generate_store_key(sc_card_t *card,
	struct sc_cardctl_setcos_gen_store_key_info *data)
{
	struct	sc_apdu apdu;
	u8	sbuf[SC_MAX_APDU_BUFFER_SIZE];
	int	r, len;

	SC_FUNC_CALLED(card->ctx, 1);

	/* Setup key-generation paramters */
	len = 0;
	if (data->op_type == OP_TYPE_GENERATE)
		sbuf[len++] = 0x92;	/* algo ID: RSA CRT */
	else
		sbuf[len++] = 0x9A;	/* algo ID: EXTERNALLY GENERATED RSA CRT */
	sbuf[len++] = 0x00;	
	sbuf[len++] = data->mod_len / 256;	/* 2 bytes for modulus bitlength */
	sbuf[len++] = data->mod_len % 256;

	sbuf[len++] = data->pubexp_len / 256;   /* 2 bytes for pubexp bitlength */
	sbuf[len++] = data->pubexp_len % 256;
	memcpy(sbuf + len, data->pubexp, (data->pubexp_len + 7) / 8);
	len += (data->pubexp_len + 7) / 8;

	if (data->op_type == OP_TYPE_STORE) {
		sbuf[len++] = data->primep_len / 256;
		sbuf[len++] = data->primep_len % 256;
		memcpy(sbuf + len, data->primep, (data->primep_len + 7) / 8);
		len += (data->primep_len + 7) / 8;
		sbuf[len++] = data->primeq_len / 256;
		sbuf[len++] = data->primeq_len % 256;
		memcpy(sbuf + len, data->primeq, (data->primeq_len + 7) / 8);
		len += (data->primeq_len + 7) / 8;		
	}

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0x46, 0x00, 0x00);
	apdu.cla = 0x00;
	apdu.data = sbuf;
	apdu.datalen = len;
	apdu.lc	= len;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "STORE/GENERATE_KEY returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

static int setcos_activate_file(sc_card_t *card)
{
	int r;
	u8 sbuf[2];
	sc_apdu_t apdu;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_1, 0x44, 0x00, 0x00);
	apdu.data = sbuf;

	r = sc_transmit_apdu(card, &apdu);
	SC_TEST_RET(card->ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	SC_TEST_RET(card->ctx, r, "ACTIVATE_FILE returned error");

	SC_FUNC_RETURN(card->ctx, 1, r);
}

static int setcos_card_ctl(sc_card_t *card, unsigned long cmd, void *ptr)
{
	if (card->type != SC_CARD_TYPE_SETCOS_44)
		return SC_ERROR_NOT_SUPPORTED;

	switch(cmd) {
	case SC_CARDCTL_SETCOS_PUTDATA:
		return setcos_putdata(card,
				(struct sc_cardctl_setcos_data_obj*) ptr);
		break;
	case SC_CARDCTL_SETCOS_GETDATA:
		return setcos_getdata(card,
				(struct sc_cardctl_setcos_data_obj*) ptr);
		break;
	case SC_CARDCTL_SETCOS_GENERATE_STORE_KEY:
		return setcos_generate_store_key(card,
				(struct sc_cardctl_setcos_gen_store_key_info *) ptr);
	case SC_CARDCTL_SETCOS_ACTIVATE_FILE:
		return setcos_activate_file(card);
	}

	return SC_ERROR_NOT_SUPPORTED;
}

static struct sc_card_driver *sc_get_driver(void)
{
	struct sc_card_driver *iso_drv = sc_get_iso7816_driver();

	setcos_ops = *iso_drv->ops;
	setcos_ops.match_card = setcos_match_card;
	setcos_ops.init = setcos_init;
	setcos_ops.finish = setcos_finish;
	if (iso_ops == NULL)
		iso_ops = iso_drv->ops;
	setcos_ops.create_file = setcos_create_file;
	setcos_ops.set_security_env = setcos_set_security_env;
	setcos_ops.select_file = setcos_select_file;
	setcos_ops.list_files = setcos_list_files;
	setcos_ops.process_fci = setcos_process_fci;
	setcos_ops.construct_fci = setcos_construct_fci;
	setcos_ops.card_ctl = setcos_card_ctl;

	return &setcos_drv;
}

#if 1
struct sc_card_driver *sc_get_setcos_driver(void)
{
	return sc_get_driver();
}
#endif

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# gen_rev.py: test oracle and fixture generator for om/tls/rev.om (not part of the product).
# Writes vectors/pki/*.der (certificates, OCSP responses, CRLs, python cryptography 41.0.7) and
# vectors/cases.txt (driver.om records). Keys are fixed scalars, so certificates and keys are stable;
# ECDSA signatures differ per run. NOW = 2026-10-27T00:00:00Z.
import calendar, datetime as dt, hashlib, os, sys
from cryptography import x509
from cryptography.x509 import ocsp
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors")
PKI = os.path.join(OUT, "pki")
os.makedirs(PKI, exist_ok=True)
NOW = dt.datetime(2026, 10, 27)
NOWU = calendar.timegm(NOW.timetuple())
H = dt.timedelta(hours=1)
D = dt.timedelta(days=1)
NB = dt.datetime(2026, 1, 1)
NA = dt.datetime(2027, 1, 1)
DP = "http://crl.example.test/iss.crl"
DPX = "http://crl.example.test/other.crl"


def key(n):
    return ec.derive_private_key(0x5EED0000 + n, ec.SECP256R1())


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def der(obj):
    return obj.public_bytes(serialization.Encoding.DER)


def save(fn, data):
    with open(os.path.join(PKI, fn), "wb") as f:
        f.write(data)
    return "pki/" + fn


def ku(cert_sign=False, crl_sign=False, ds=True):
    return x509.KeyUsage(ds, False, False, False, False, cert_sign, crl_sign, False, False)


def cert(cn, k, issuer_cn, ik, serial, ca=False, kus=None, eku=None, dp=None, tlsf=False,
         nb=NB, na=NA, san=False):
    b = (x509.CertificateBuilder().subject_name(name(cn)).issuer_name(name(issuer_cn))
         .public_key(k.public_key()).serial_number(serial).not_valid_before(nb).not_valid_after(na)
         .add_extension(x509.BasicConstraints(ca=ca, path_length=0 if ca and issuer_cn != cn else None),
                        critical=True))
    if kus is not None:
        b = b.add_extension(kus, critical=True)
    if eku is not None:
        b = b.add_extension(x509.ExtendedKeyUsage(eku), critical=False)
    if dp is not None:
        b = b.add_extension(x509.CRLDistributionPoints([x509.DistributionPoint(
            [x509.UniformResourceIdentifier(dp)], None, None, None)]), critical=False)
    if tlsf:
        b = b.add_extension(x509.TLSFeature([x509.TLSFeatureType.status_request]), critical=False)
    if san:
        b = b.add_extension(x509.SubjectAlternativeName([x509.DNSName("www.example.test")]),
                            critical=False)
    return b.sign(ik, hashes.SHA256())


kR, kI, kB, kL, kO = key(1), key(2), key(3), key(4), key(5)
root = cert("Rev Test Root", kR, "Rev Test Root", kR, 1, ca=True, kus=ku(True, True, False),
            na=dt.datetime(2036, 1, 1))
iss = cert("Rev Test Issuer", kI, "Rev Test Root", kR, 2, ca=True, kus=ku(True, True, True))
issB = cert("Rev Test Issuer B", kB, "Rev Test Root", kR, 3, ca=True, kus=ku(True, False, True))
SAUTH = [ExtendedKeyUsageOID.SERVER_AUTH]
leaf = cert("www.example.test", kL, "Rev Test Issuer", kI, 0x1001, kus=ku(), eku=SAUTH, dp=DP, san=True)
leafMS = cert("www.example.test", kL, "Rev Test Issuer", kI, 0x1002, kus=ku(), eku=SAUTH, dp=DP,
              tlsf=True, san=True)
leafND = cert("www.example.test", kL, "Rev Test Issuer", kI, 0x1003, kus=ku(), eku=SAUTH, san=True)
BIG = (1 << 158) + 0x8F12345
leafBig = cert("www.example.test", kL, "Rev Test Issuer", kI, BIG, kus=ku(), eku=SAUTH, dp=DP, san=True)
leafB = cert("www.example.test", kL, "Rev Test Issuer B", kB, 0x2001, kus=ku(), eku=SAUTH, san=True)
OSIGN = [ExtendedKeyUsageOID.OCSP_SIGNING]
resp = cert("Rev OCSP Responder", kO, "Rev Test Issuer", kI, 0x3001, kus=ku(), eku=OSIGN)
respNE = cert("Rev OCSP Responder", kO, "Rev Test Issuer", kI, 0x3002, kus=ku())
respRoot = cert("Rev OCSP Responder", kO, "Rev Test Root", kR, 0x3003, kus=ku(), eku=OSIGN)
respExp = cert("Rev OCSP Responder", kO, "Rev Test Issuer", kI, 0x3004, kus=ku(), eku=OSIGN,
               na=dt.datetime(2026, 6, 1))

F = {}
for n, c in [("root", root), ("iss", iss), ("issB", issB), ("leaf", leaf), ("leafMS", leafMS),
             ("leafND", leafND), ("leafBig", leafBig), ("leafB", leafB), ("resp", resp),
             ("respNE", respNE), ("respRoot", respRoot), ("respExp", respExp)]:
    F[n] = save(n + ".der", der(c))
with open(os.path.join(PKI, "store.pem"), "wb") as f:
    f.write(root.public_bytes(serialization.Encoding.PEM))


def ocspr(fn, subj=leaf, status=ocsp.OCSPCertStatus.GOOD, tu=NOW - H, nu=NOW + 3 * D, alg=hashes.SHA1(),
          signer=iss, sk=kI, enc=ocsp.OCSPResponderEncoding.NAME, certs=None, ext=None, issuer=iss):
    b = ocsp.OCSPResponseBuilder().add_response(
        cert=subj, issuer=issuer, algorithm=alg, cert_status=status, this_update=tu, next_update=nu,
        revocation_time=NOW - 10 * D if status == ocsp.OCSPCertStatus.REVOKED else None,
        revocation_reason=x509.ReasonFlags.key_compromise if status == ocsp.OCSPCertStatus.REVOKED else None)
    b = b.responder_id(enc, signer)
    if certs:
        b = b.certificates(certs)
    if ext is not None:
        b = b.add_extension(ext[0], critical=ext[1])
    r = b.sign(sk, hashes.SHA256())
    return save(fn, r.public_bytes(serialization.Encoding.DER))


def flip_last(path, delta=1):
    p = os.path.join(OUT, path)
    d = bytearray(open(p, "rb").read())
    d[-1] ^= delta
    open(p, "wb").write(bytes(d))
    return path


GOOD, REVOKED, UNKNOWN = ocsp.OCSPCertStatus.GOOD, ocsp.OCSPCertStatus.REVOKED, ocsp.OCSPCertStatus.UNKNOWN
S = {}
S["good"] = ocspr("o_good.der")
S["good_bykey"] = ocspr("o_good_bykey.der", enc=ocsp.OCSPResponderEncoding.HASH)
S["good_sha256"] = ocspr("o_good_sha256.der", alg=hashes.SHA256())
S["good_nonce"] = ocspr("o_good_nonce.der", ext=(x509.OCSPNonce(b"0123456789abcdef"), False))
S["crit_nonce"] = ocspr("o_crit_nonce.der", ext=(x509.OCSPNonce(b"0123456789abcdef"), True))
S["revoked"] = ocspr("o_revoked.der", status=REVOKED)
S["revoked_stale"] = ocspr("o_revoked_stale.der", status=REVOKED, tu=NOW - 5 * D, nu=NOW - D)
S["unknown"] = ocspr("o_unknown.der", status=UNKNOWN)
S["stale"] = ocspr("o_stale.der", tu=NOW - 5 * D, nu=NOW - H)
S["stale_edge"] = ocspr("o_stale_edge.der", tu=NOW - 5 * D, nu=NOW)
S["future"] = ocspr("o_future.der", tu=NOW + 301 * dt.timedelta(seconds=1))
S["skew_ok"] = ocspr("o_skew_ok.der", tu=NOW + 300 * dt.timedelta(seconds=1))
S["no_next"] = ocspr("o_no_next.der", nu=None)
S["other_serial"] = ocspr("o_other_serial.der", subj=leafND)
S["other_issuer"] = ocspr("o_other_issuer.der", subj=leafB, issuer=issB, signer=issB, sk=kB)
S["bad_sig"] = flip_last(ocspr("o_bad_sig.der"))
issR = cert("Rev Test Issuer", kR, "Rev Test Root", kR, 9, ca=True, kus=ku(True, True, True))
S["wrong_key"] = ocspr("o_wrong_key.der", signer=issR, sk=kR)
S["deleg"] = ocspr("o_deleg.der", signer=resp, sk=kO, certs=[resp])
S["deleg_bykey"] = ocspr("o_deleg_bykey.der", signer=resp, sk=kO, certs=[resp],
                         enc=ocsp.OCSPResponderEncoding.HASH)
S["deleg_revoked"] = ocspr("o_deleg_revoked.der", status=REVOKED, signer=resp, sk=kO, certs=[resp])
S["deleg_noeku"] = ocspr("o_deleg_noeku.der", signer=respNE, sk=kO, certs=[respNE])
S["deleg_root"] = ocspr("o_deleg_root.der", signer=respRoot, sk=kO, certs=[respRoot])
S["deleg_expired"] = ocspr("o_deleg_expired.der", signer=respExp, sk=kO, certs=[respExp])
S["deleg_nocert"] = ocspr("o_deleg_nocert.der", signer=resp, sk=kO)
S["ms_good"] = ocspr("o_ms_good.der", subj=leafMS)
S["big_good"] = ocspr("o_big_good.der", subj=leafBig)
S["big_revoked"] = ocspr("o_big_revoked.der", subj=leafBig, status=REVOKED)
S["trylater"] = save("o_trylater.der", ocsp.OCSPResponseBuilder.build_unsuccessful(
    ocsp.OCSPResponseStatus.TRY_LATER).public_bytes(serialization.Encoding.DER))
S["unauth"] = save("o_unauth.der", ocsp.OCSPResponseBuilder.build_unsuccessful(
    ocsp.OCSPResponseStatus.UNAUTHORIZED).public_bytes(serialization.Encoding.DER))
S["garbage"] = save("o_garbage.der", b"\x30\x03\x0a\x01\x00")


def crl(fn, serials=(0x1004, 0x1005), lu=NOW - H, nu=NOW + 7 * D, ik=kI, iname="Rev Test Issuer",
        idp=None, delta=False, entry_ext=None, nonext=False):
    b = (x509.CertificateRevocationListBuilder().issuer_name(name(iname)).last_update(lu))
    if nu is not None:
        b = b.next_update(nu)
    for s in serials:
        rb = x509.RevokedCertificateBuilder().serial_number(s).revocation_date(NOW - 2 * D)
        if entry_ext is not None:
            rb = rb.add_extension(entry_ext[0], critical=entry_ext[1])
        b = b.add_revoked_certificate(rb.build())
    if not nonext:
        b = b.add_extension(x509.CRLNumber(7), critical=False)
    if idp is not None:
        b = b.add_extension(idp, critical=True)
    if delta:
        b = b.add_extension(x509.DeltaCRLIndicator(6), critical=True)
    return save(fn, b.sign(ik, hashes.SHA256()).public_bytes(serialization.Encoding.DER))


def idp(uri=DP, users=False, cas=False):
    return x509.IssuingDistributionPoint([x509.UniformResourceIdentifier(uri)], None, users, cas, None,
                                         False, False)


C = {}
C["revoked"] = crl("c_revoked.der", serials=(0x1004, 0x1001, 0x1005))
C["clean"] = crl("c_clean.der")
C["empty"] = crl("c_empty.der", serials=())
C["v1"] = crl("c_v1.der", serials=(0x1001,), nonext=True)
C["stale"] = crl("c_stale.der", lu=NOW - 9 * D, nu=NOW - D)
C["future"] = crl("c_future.der", lu=NOW + D)
C["wrong_name"] = crl("c_wrong_name.der", iname="Rev Test Other")
C["root_signed"] = crl("c_root_signed.der", ik=kR, iname="Rev Test Root", serials=(0x1001,))
C["bad_sig"] = flip_last(crl("c_bad_sig.der", serials=(0x1001,)))
C["idp_match"] = crl("c_idp_match.der", idp=idp())
C["idp_match_rev"] = crl("c_idp_match_rev.der", idp=idp(users=True), serials=(0x1001,))
C["idp_other"] = crl("c_idp_other.der", idp=idp(DPX))
C["idp_onlyca"] = crl("c_idp_onlyca.der", idp=idp(cas=True))
C["delta"] = crl("c_delta.der", delta=True)
C["entry_reason"] = crl("c_entry_reason.der", serials=(0x1001,),
                        entry_ext=(x509.CRLReason(x509.ReasonFlags.key_compromise), False))
C["entry_ci"] = crl("c_entry_ci.der", serials=(0x1001,),
                    entry_ext=(x509.CertificateIssuer([x509.DNSName("other.test")]), True))
C["big"] = crl("c_big.der", serials=(0x1001, BIG))
C["issB"] = crl("c_issB.der", ik=kB, iname="Rev Test Issuer B", serials=(0x2001,))

L, I, LM, LN, LG, LB = F["leaf"], F["iss"], F["leafMS"], F["leafND"], F["leafBig"], F["leafB"]
OK = "X509_OK"
cases = [
    ("O", "ocsp_good", OK, L, I, S["good"], "-"),
    ("O", "ocsp_good_bykey", OK, L, I, S["good_bykey"], "-"),
    ("O", "ocsp_good_sha256_certid", OK, L, I, S["good_sha256"], "-"),
    ("O", "ocsp_good_nonce_ext", OK, L, I, S["good_nonce"], "-"),
    ("O", "ocsp_critical_nonce", "X509_ERR_CRITICAL_EXT", L, I, S["crit_nonce"], "-"),
    ("O", "ocsp_revoked", "REV_ERR_REVOKED", L, I, S["revoked"], "-"),
    ("O", "ocsp_revoked_beats_stale", "REV_ERR_REVOKED", L, I, S["revoked_stale"], "-"),
    ("O", "ocsp_unknown", "REV_ERR_STATUS_UNKNOWN", L, I, S["unknown"], "-"),
    ("O", "ocsp_stale", "REV_ERR_STALE", L, I, S["stale"], "-"),
    ("O", "ocsp_next_update_equals_now", "REV_ERR_STALE", L, I, S["stale_edge"], "-"),
    ("O", "ocsp_future_301s", "REV_ERR_STALE", L, I, S["future"], "-"),
    ("O", "ocsp_future_300s_skew", OK, L, I, S["skew_ok"], "-"),
    ("O", "ocsp_no_next_update", "REV_ERR_STALE", L, I, S["no_next"], "-"),
    ("O", "ocsp_other_serial", "REV_ERR_STATUS_UNKNOWN", L, I, S["other_serial"], "-"),
    ("O", "ocsp_other_issuer", "REV_ERR_RESPONDER", L, I, S["other_issuer"], "-"),
    ("O", "ocsp_bad_sig", "X509_ERR_BAD_SIGNATURE", L, I, S["bad_sig"], "-"),
    ("O", "ocsp_wrong_key", "X509_ERR_BAD_SIGNATURE", L, I, S["wrong_key"], "-"),
    ("O", "ocsp_delegated", OK, L, I, S["deleg"], "-"),
    ("O", "ocsp_delegated_bykey", OK, L, I, S["deleg_bykey"], "-"),
    ("O", "ocsp_delegated_revoked", "REV_ERR_REVOKED", L, I, S["deleg_revoked"], "-"),
    ("O", "ocsp_delegated_no_eku", "REV_ERR_RESPONDER", L, I, S["deleg_noeku"], "-"),
    ("O", "ocsp_delegated_by_root", "REV_ERR_RESPONDER", L, I, S["deleg_root"], "-"),
    ("O", "ocsp_delegated_expired", "REV_ERR_RESPONDER", L, I, S["deleg_expired"], "-"),
    ("O", "ocsp_delegated_cert_missing", "REV_ERR_RESPONDER", L, I, S["deleg_nocert"], "-"),
    ("O", "ocsp_try_later", "REV_ERR_OCSP_STATUS", L, I, S["trylater"], "-"),
    ("O", "ocsp_unauthorized", "REV_ERR_OCSP_STATUS", L, I, S["unauth"], "-"),
    ("O", "ocsp_garbage", "X509_ERR_MALFORMED", L, I, S["garbage"], "-"),
    ("O", "ocsp_big_serial_good", OK, LG, I, S["big_good"], "-"),
    ("O", "ocsp_big_serial_revoked", "REV_ERR_REVOKED", LG, I, S["big_revoked"], "-"),
    ("O", "ocsp_staple_for_other_leaf", "REV_ERR_STATUS_UNKNOWN", LG, I, S["good"], "-"),
    ("O", "must_staple_missing", "REV_ERR_NO_STAPLE", LM, I, "-", "-"),
    ("O", "must_staple_missing_crl_clean", "REV_ERR_NO_STAPLE", LM, I, "-", C["clean"]),
    ("O", "must_staple_good", OK, LM, I, S["ms_good"], "-"),
    ("O", "must_staple_wrong_staple", "REV_ERR_STATUS_UNKNOWN", LM, I, S["good"], "-"),
    ("O", "no_staple_no_crl_soft", OK, L, I, "-", "-"),
    ("O", "crl_revoked", "REV_ERR_REVOKED", L, I, "-", C["revoked"]),
    ("O", "crl_clean", OK, L, I, "-", C["clean"]),
    ("O", "crl_empty_list", OK, L, I, "-", C["empty"]),
    ("O", "crl_v1_listed", "REV_ERR_REVOKED", L, I, "-", C["v1"]),
    ("O", "crl_stale", "REV_ERR_STALE", L, I, "-", C["stale"]),
    ("O", "crl_future", "REV_ERR_STALE", L, I, "-", C["future"]),
    ("O", "crl_wrong_issuer_name", "REV_ERR_CRL_SCOPE", L, I, "-", C["wrong_name"]),
    ("O", "crl_signed_by_root", "X509_ERR_BAD_SIGNATURE", L, I, "-", C["root_signed"]),
    ("O", "crl_bad_sig", "X509_ERR_BAD_SIGNATURE", L, I, "-", C["bad_sig"]),
    ("O", "crl_idp_match_clean", OK, L, I, "-", C["idp_match"]),
    ("O", "crl_idp_match_revoked", "REV_ERR_REVOKED", L, I, "-", C["idp_match_rev"]),
    ("O", "crl_idp_other_dp", "REV_ERR_CRL_SCOPE", L, I, "-", C["idp_other"]),
    ("O", "crl_idp_leaf_without_dp", "REV_ERR_CRL_SCOPE", LN, I, "-", C["idp_match"]),
    ("O", "crl_idp_only_ca", "REV_ERR_CRL_SCOPE", L, I, "-", C["idp_onlyca"]),
    ("O", "crl_delta", "REV_ERR_CRL_SCOPE", L, I, "-", C["delta"]),
    ("O", "crl_entry_reason", "REV_ERR_REVOKED", L, I, "-", C["entry_reason"]),
    ("O", "crl_entry_cert_issuer", "REV_ERR_CRL_SCOPE", L, I, "-", C["entry_ci"]),
    ("O", "crl_big_serial_revoked", "REV_ERR_REVOKED", LG, I, "-", C["big"]),
    ("O", "crl_big_serial_other", OK, LN, I, "-", C["big"]),
    ("O", "crl_issuer_without_crlsign", "X509_ERR_KEY_USAGE", LB, F["issB"], "-", C["issB"]),
    ("O", "staple_good_crl_revoked", "REV_ERR_REVOKED", L, I, S["good"], C["revoked"]),
    ("O", "staple_revoked_crl_clean", "REV_ERR_REVOKED", L, I, S["revoked"], C["clean"]),
]

# Real Let's Encrypt CRL: real/ye2-74.crl (http://ye2.c.lencr.org/74.crl, thisUpdate
# 2026-10-26T23:03:21Z, 1575 entries) and its real issuer real/ye2.der
# (https://letsencrypt.org/certs/gen-y/int-ye2.der). The leaves are synthetic: issuer Name copied
# from YE2, the CRL's distribution point, a listed serial (or an unlisted one), signed by a test key
# (rev.om does not check the leaf signature; x509_chain_verify does that before it).
ye2 = x509.load_der_x509_certificate(open(os.path.join(OUT, "real", "ye2.der"), "rb").read())


def leye2(fn, serial, dp):
    c = (x509.CertificateBuilder().subject_name(name("www.example.test")).issuer_name(ye2.subject)
         .public_key(kL.public_key()).serial_number(serial).not_valid_before(NB).not_valid_after(NA)
         .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
         .add_extension(x509.CRLDistributionPoints([x509.DistributionPoint(
             [x509.UniformResourceIdentifier(dp)], None, None, None)]), critical=False)
         .sign(kI, hashes.SHA256()))
    return save(fn, der(c))


YE = "real/ye2.der"
YC = "real/ye2-74.crl"
cases += [
    ("O", "le_crl_listed_serial", "REV_ERR_REVOKED",
     leye2("leYE2rev.der", 0x050A72B7AE90C2E1C61BB7D1568032F79549, "http://ye2.c.lencr.org/74.crl"),
     YE, "-", YC),
    ("O", "le_crl_unlisted_serial", OK,
     leye2("leYE2ok.der", 0x050A72B7AE90C2E1C61BB7D1568032F7954A, "http://ye2.c.lencr.org/74.crl"),
     YE, "-", YC),
    ("O", "le_crl_other_shard", "REV_ERR_CRL_SCOPE",
     leye2("leYE2shard.der", 0x050A72B7AE90C2E1C61BB7D1568032F79549, "http://ye2.c.lencr.org/75.crl"),
     YE, "-", YC),
    ("O", "le_crl_wrong_issuer_key", "X509_ERR_BAD_SIGNATURE", "pki/leYE2rev.der", I, "-", YC),
]
lines = ["# om/tls/rev.om test cases (driver.om records; generated by ../gen_rev.py, do not edit).",
         "# NOW = %d = 2026-10-27T00:00:00Z. Test certificates valid 2026-01-01 .. 2027-01-01." % NOWU]
for c in cases:
    lines.append("O %s %s %d %s %s %s %s" % (c[1], c[2], NOWU, c[3], c[4], c[5], c[6]))
lines.append("O now_after_9999 X509_ERR_MALFORMED 253402300800 %s %s %s -" % (L, I, S["good"]))
lines.append("O le_crl_after_next_update REV_ERR_REVOKED %d pki/leYE2rev.der %s - %s" % (
    calendar.timegm(dt.datetime(2026, 11, 5).timetuple()), YE, YC))
lines.append("O le_crl_before_this_update REV_ERR_REVOKED %d pki/leYE2rev.der %s - %s" % (
    calendar.timegm(dt.datetime(2026, 10, 26, 22).timetuple()), YE, YC))
lines.append("O le_crl_unlisted_after_next_update REV_ERR_STALE %d pki/leYE2ok.der %s - %s" % (
    calendar.timegm(dt.datetime(2026, 11, 5).timetuple()), YE, YC))
lines.append("G fuzz_le_crl 200 %d pki/leYE2rev.der %s %s" % (NOWU, YE, YC))
for n, w, f in [("mustst_leaf", "1", LM), ("mustst_plain", "0", L), ("mustst_issuer", "0", I)]:
    lines.append("U %s %s %s" % (n, w, f))
lines.append("I issuer_from_list %s pki/store.pem %s %s" % (I, L, I))
lines.append("I issuer_after_unrelated %s pki/store.pem %s %s %s" % (I, L, F["issB"], I))
lines.append("I issuer_absent - pki/store.pem %s %s" % (L, F["issB"]))
lines.append("I issuer_from_store %s pki/store.pem %s" % (F["root"], I))
for m in [b"", b"abc", b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          bytes(range(256)) * 3]:
    lines.append("H sha1_len%d %s %s" % (len(m), m.hex() or "-", hashlib.sha1(m).hexdigest()))
lines.append("A sha1_million_a 1000000 97 %s" % hashlib.sha1(b"a" * 1000000).hexdigest())
for code, nm in enumerate(["REV_ERR_REVOKED", "REV_ERR_STATUS_UNKNOWN", "REV_ERR_STALE",
                           "REV_ERR_NO_STAPLE", "REV_ERR_RESPONDER", "REV_ERR_OCSP_STATUS",
                           "REV_ERR_CRL_SCOPE"]):
    lines.append("E errname_%d %s %d" % (32 + code, nm, 32 + code))
lines.append("E errname_6 X509_ERR_BAD_SIGNATURE 6")
lines.append("E errname_39 - 39")
lines.append("F fuzz_staple_good 400 %d %s %s %s" % (NOWU, L, I, S["good"]))
lines.append("F fuzz_staple_deleg 400 %d %s %s %s" % (NOWU, L, I, S["deleg"]))
lines.append("G fuzz_crl_revoked 400 %d %s %s %s" % (NOWU, L, I, C["revoked"]))
with open(os.path.join(OUT, "cases.txt"), "w") as f:
    f.write("\n".join(lines) + "\n")
print("gen_rev: %d records" % (len(lines) - 2))

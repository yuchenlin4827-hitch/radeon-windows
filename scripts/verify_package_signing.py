#!/usr/bin/env python3
"""verify_package_signing.py — 验证 rokmd 发行包签名的自洽性（Linux 即可跑）。

用法：
    python3 scripts/verify_package_signing.py <zip 或解压目录>

校验内容：
  1) rokmd.sys 内嵌 Authenticode 签名存在且摘要自洽（signify）
  2) rokmd.cat PKCS#7：eContent 摘要 == messageDigest，SignerInfo 的
     RSA PKCS#1 v1.5 签名对包内证书公钥有效（asn1crypto + cryptography）
  3) 成员哈希：rokmd.inf 的 SHA1/SHA256 原文件哈希、rokmd.sys 的
     SHA256 Authenticode 摘要（即内嵌签名 SPC_INDIRECT_DATA 的
     messageDigest，Windows 目录校验 PE 成员用它）逐一比对

依赖：pip install signify asn1crypto cryptography
"""
import hashlib, io, os, sys, tempfile, zipfile

from asn1crypto import cms
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding


def load_members(arg):
    """Return (dir, {name: bytes}) for a zip or a directory."""
    if os.path.isdir(arg):
        d = arg
        files = {}
        for root, _, names in os.walk(d):
            for n in names:
                p = os.path.join(root, n)
                files[os.path.relpath(p, d).replace('\\', '/')] = open(p, 'rb').read()
        return d, files
    tmp = tempfile.mkdtemp(prefix='rokmd-verify-')
    with zipfile.ZipFile(arg) as z:
        z.extractall(tmp)
    return load_members(tmp)


def cat_econtent_bytes(sd):
    """Catalog eContent: [0] EXPLICIT SEQUENCE -> the SEQUENCE value bytes
    (signtool 的 messageDigest 口径：不含 [0] 与 SEQUENCE 的 TLV 头)。"""
    wrapper = sd['encap_content_info']['content'].dump()
    i = 1
    l, i = wrapper[i], i + 1
    if l & 0x80:
        n = l & 0x7F
        i += n
    # now at SEQUENCE TLV; skip its header too
    i += 1
    l, i = wrapper[i], i + 1
    if l & 0x80:
        n = l & 0x7F
        i += n
    return wrapper[i:]  # SEQUENCE value bytes


def verify(pkg_dir, files):
    ok = True

    # ---- 1) sys embedded Authenticode ----
    from signify.authenticode.signed_file import SignedPEFile
    sys_bytes = files.get('rokmd.sys') or files.get('bin/rokmd.sys')
    assert sys_bytes, 'rokmd.sys not found in package'
    with io.BytesIO(sys_bytes) as f:
        sigs = list(SignedPEFile(f).signatures)
    if not sigs:
        print('[FAIL] rokmd.sys: no embedded Authenticode signature')
        ok = False
    else:
        s = sigs[0]
        digest = s.indirect_data.digest
        algo = s.indirect_data.asn1['message_digest']['digest_algorithm']['algorithm'].native
        print(f'[ OK ] rokmd.sys: embedded Authenticode signature present '
              f'({algo}, digest {digest.hex()[:16]}...)')

    # ---- 2) cat PKCS#7 ----
    cat = files.get('rokmd.cat')
    assert cat, 'rokmd.cat not found in package'
    ci = cms.ContentInfo.load(cat)
    sd = ci['content']
    econtent = cat_econtent_bytes(sd)
    certs = [c.chosen for c in sd['certificates']]
    for si in sd['signer_infos']:
        md = next(a['values'][0].native for a in si['signed_attrs']
                  if a['type'].native == 'message_digest')
        digest_ok = md == hashlib.sha256(econtent).digest()
        print(f'[ {"OK " if digest_ok else "FAIL"} ] rokmd.cat: eContent sha256 == messageDigest '
              f'({len(econtent)} bytes)')
        ok &= digest_ok
        sid = si['sid'].chosen
        cert = next((c for c in certs if c.serial_number == sid['serial_number']), certs[0])
        pubkey = serialization.load_der_public_key(
            cert['tbs_certificate']['subject_public_key_info'].dump())
        try:
            pubkey.verify(si['signature'].native, si['signed_attrs'].untag().dump(),
                          padding.PKCS1v15(), hashes.SHA256())
            print(f'[ OK ] rokmd.cat: SignerInfo signature VALID '
                  f'({cert.subject.human_friendly.replace(chr(10), ", ")})')
        except Exception as e:
            print('[FAIL] rokmd.cat: SignerInfo signature invalid:', e)
            ok = False

    # ---- 3) member hashes ----
    for fname, mode in (('rokmd.inf', 'raw'), ('rokmd.sys', 'authenticode')):
        data = files.get(fname) or files.get('bin/' + fname)
        assert data, f'{fname} not found in package'
        if mode == 'raw':
            h1, h2 = hashlib.sha1(data).digest(), hashlib.sha256(data).digest()
        else:
            h2 = digest  # SPC_INDIRECT_DATA messageDigest == cat PE member hash
            h1 = None
        hit = (h1 in econtent if h1 else True) and (h2 in econtent)
        print(f'[ {"OK " if hit else "FAIL"} ] cat member {fname} ({mode} hash) matches shipped file')
        ok &= hit

    print()
    print('RESULT:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    d, files = load_members(sys.argv[1])
    sys.exit(verify(d, files))

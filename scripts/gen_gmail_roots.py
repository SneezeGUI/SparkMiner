"""Select currently trusted Google CA roots from the pinned Mozilla PEM store."""
from pathlib import Path
import hashlib,json
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives.serialization import Encoding
root=Path(__file__).resolve().parents[1]/'src/mail'
selected=[]
for cert in x509.load_pem_x509_certificates((root/'cacert.pem').read_bytes()):
 names=cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
 if names and names[0].value.startswith('GTS Root R'):
  assert cert.subject==cert.issuer and cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca
  selected.append((names[0].value,cert))
assert any(name=='GTS Root R4' for name,_ in selected)
data=b''.join(cert.public_bytes(Encoding.PEM) for name,cert in sorted(selected))
(root/'gmail_roots.pem').write_bytes(data)
p=root/'trust-bundle.json';m=json.loads(p.read_text());m['gmail_pem_roots']=[n for n,c in sorted(selected)];m['gmail_pem_sha256']=hashlib.sha256(data).hexdigest();m['gmail_verification']='Standard mbedTLS CA chain and hostname verification; no bundle callback for smtp.gmail.com.';p.write_text(json.dumps(m,indent=2)+'\n')
print(m['gmail_pem_roots'])

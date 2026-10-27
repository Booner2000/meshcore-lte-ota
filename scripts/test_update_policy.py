"""Credential-boundary regression checks for intentionally unauthenticated OTA.

Static checks complement the real server and hardware build tests: secrets must
never enter a TLS context configured for unverified firmware downloads.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=root/'lib/LTEUpdate/src'
w=(m/'WifiHttp.cpp').read_text()
a=(m/'A7670Http.cpp').read_text()
u=(m/'UpdateSession.cpp').read_text()
def body(text,start,end):
 return text.split(start,1)[1].split(end,1)[0]
checks=0
def check(condition,message):
 global checks
 checks+=1
 assert condition,message
sync=body(w,'void WifiConnection::sync','WifiHttp::WifiHttp')
update=body(w,'bool WifiHttp::begin','bool WifiHttp::response')
helper=body(w,'void secure(','}\n}')
check('bool verifyPeer=true' in helper,'Management TLS must default to verification')
check('if(verifyPeer)client.setCACert(RootCa);else client.setInsecure();' in helper,'Separate trusted and untrusted TLS modes')
check('secure(client,http,false)' in update,'Firmware deliberately skips certificate validation')
check('setAuthorization' not in update,'No management credentials on firmware GETs')
check(sync.count('secure(client,http);')==2 and 'secure(client,http,false)' not in sync,'Configuration GET and ACK must verify certificates')
check('!setClock(platform)' in sync,'Configuration certificate dates require valid UTC')
check('setAuthorization(config.username,config.password)' in sync,'Configuration remains authenticated')
check('bool A7670Http::prepare() { return prepareWithPeerVerification(false); }' in a,'LTE firmware uses unverified context')
refresh=body(a,'Error A7670Http::refreshWifiConfig','\n}\n}')
check(refresh.index('prepareWithPeerVerification(true)')<refresh.index('Authorization: Basic'),'LTE management TLS must verify before sending private Authorization')
verified=body(a,'  if(verifyPeer){','  }else{')
check('if (!certificate()) return false;' in verified and '\\"authmode\\",0,1' in verified and '\\"ignorecertCN\\",0,0' in verified,'LTE management CA and hostname checks remain enabled')
check('Ed25519::verify' not in u and 'TrustedKey.h' not in u and 'signed_payload' not in u,'Firmware reads unsigned top-level manifest only')
check('release<=otaReleaseFloor()' in u and 'Error::HashMismatch' in u and 'capabilitySeen' in u,'Release, SHA-256 and capability guards remain')
print(f'PASS: {checks} firmware/WLAN-secret boundary checks')

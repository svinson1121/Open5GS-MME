<p align="center"><a href="https://open5gs.org" target="_blank" rel="noopener noreferrer"><img width="100" src="https://open5gs.org/assets/img/open5gs-logo-only.png" alt="Open5GS logo"></a></p>

## Getting Started

Please follow the [documentation](https://open5gs.org/open5gs/docs/) at [open5gs.org](https://open5gs.org/)!



## License

- Open5GS Open Source files are made available under the terms of the GNU Affero General Public License ([GNU AGPL v3.0](https://www.gnu.org/licenses/agpl-3.0.html)).
- [Commercial licenses](https://open5gs.org/open5gs/support/) are also available from [NeoPlane](https://neoplane.io/)
#####################################################

I recommend running only the MME from this code. Use the SGW C/U and PGW C/U from the main Open5GS project or eUPF.
* Forked https://github.com/Omnitouch/open5gs 
* Fixed random code issue so it would build on:  Ubuntu 24.10 @svinson1121
* Change SGW DNS selection to use the correct 3GPP format: tac-lb(NN).tac-hb(NN).tac.epc.mnc(NN(N)).mcc(NNN).3gppnetwork.org @svinson1121
  




Example DNS zones:
SGW:
```
$ORIGIN tac.epc.mnc435.mcc311.3gppnetwork.org.
$TTL 1W
@                       1D IN SOA       localhost. root.localhost. (
                                        2025073106      ; serial
                                        3H              ; refresh
                                        15M             ; retry
                                        1W              ; expiry
                                        1D )            ; minimum

                        1D IN NS        core-dns.mnc435.mcc311.3gppnetwork.org.
                        1D IN NS        core-ns2.mnc435.mcc311.3gppnetwork.org.


tac-lb01.tac-hb00       IN NAPTR 102 10 "a" "x-3gpp-sgw:x-s11" ""    sgw-c.epc.mnc435.mcc311.3gppnetwork.org.
tac-lb01.tac-hb00       IN NAPTR 102 20 "a" "x-3gpp-sgw:x-s5-gtp" ""  sgw-c.epc.mnc435.mcc311.3gppnetwork.org.
```



Example DNS zones: PGW:
```
$ORIGIN apn.epc.mnc435.mcc311.3gppnetwork.org.
$TTL 1W
@                       1D IN SOA       localhost. root.localhost. (
                                        2025072801      ; serial
                                        3H              ; refresh
                                        15M             ; retry
                                        1W              ; expiry
                                        1D )            ; minimum

                        1D IN NS        core-dns.mnc435.mcc311.3gppnetwork.org.
                        1D IN NS        core-ns2.mnc435.mcc311.3gppnetwork.org.

ims             IN NAPTR 100 999 "a" "x-3gpp-pgw:x-s5-gtp:x-s8-gtp" "" smf.epc.mnc435.mcc311.3gppnetwork.org.
internet        IN NAPTR 100 999 "a" "x-3gpp-pgw:x-s5-gtp:x-s8-gtp" "" smf.epc.mnc435.mcc311.3gppnetwork.org.
mms             IN NAPTR 100 999 "a" "x-3gpp-pgw:x-s5-gtp:x-s8-gtp" "" smf.epc.mnc435.mcc311.3gppnetwork.org.
```



* Back ported the HSS_MAP  from https://github.com/open5gs/open5gs/commit/9d83eba550bd693bd18568d60189b14161b97f06      @svinson1121

* added SGs NAM patch https://github.com/open5gs/open5gs/commit/390a9dd6375858abfa93ff511d389d2bc41c4eca

* Fixed MME (SGs-AP) crash when the MSC connection was lost or shutdown. MME will now try to Reconnect to the MSC.

#TODO:
* add support for PGW topon / topoff selection ETSI TS 129 303
  
#Known Issues


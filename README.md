<p align="center"><a href="https://open5gs.org" target="_blank" rel="noopener noreferrer"><img width="100" src="https://open5gs.org/assets/img/open5gs-logo-only.png" alt="Open5GS logo"></a></p>

## Getting Started

Please follow the [documentation](https://open5gs.org/open5gs/docs/) at [open5gs.org](https://open5gs.org/)!

## Sponsors

If you find Open5GS useful for work, please consider supporting this Open Source project by [Becoming a sponsor](https://github.com/sponsors/acetcom). To manage the funding transactions transparently, you can donate through [OpenCollective](https://opencollective.com/open5gs).

<p align="center">
  <a target="_blank" href="https://open5gs.org/#sponsors">
      <img alt="sponsors" src="https://open5gs.org/assets/img/sponsors.svg">
  </a>
</p>

## Community

- Problem with Open5GS can be filed as [issues](https://github.com/open5gs/open5gs/issues) in this repository.
- Other topics related to this project are happening on the [discussions](https://github.com/open5gs/open5gs/discussions).
- Voice and text chat are available in Open5GS's [Discord](https://discordapp.com/) workspace. Use [this link](https://discord.gg/GreNkuc) to get started.

## Contributing

If you're contributing through a pull request to Open5GS project on GitHub, please read the [Contributor License Agreement](https://open5gs.org/open5gs/cla/) in advance.

## License

- Open5GS Open Source files are made available under the terms of the GNU Affero General Public License ([GNU AGPL v3.0](https://www.gnu.org/licenses/agpl-3.0.html)).
- [Commercial licenses](https://open5gs.org/open5gs/support/) are also available from [NeoPlane](https://neoplane.io/)
#####################################################
* Change SGW DNS selection to use the correct 3gpp format of tac-lb(NN).tac-hb(NN).tac.epc.mnc(NN(N)).mcc(NNN).3gppnetwork.org

Example DNS zones:
SGW:

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




PGW:

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
ims             IN NAPTR 100 999 "a" "x-3gpp-sgw:x-s5-gtp:x-s8-gtp:x-s11" ""  sgw-c.epc.mnc435.mcc311.3gppnetwork.org.
internet        IN NAPTR 100 999 "a" "x-3gpp-pgw:x-s5-gtp:x-s8-gtp" "" smf.epc.mnc435.mcc311.3gppnetwork.org.
internet        IN NAPTR 100 999 "a" "x-3ggp-sgw:x-s5-gtp:x-s8-gtp:x-s11" ""  sgw-c.epc.mnc435.mcc311.3gppnetwork.org.
mms             IN NAPTR 100 999 "a" "x-3gpp-pgw:x-s5-gtp:x-s8-gtp" "" smf.epc.mnc435.mcc311.3gppnetwork.org.
mms             IN NAPTR 100 999 "a" "x-3gpp-sgw:x-s5-gtp:x-s8-gtp:x-s11" ""  sgw-c.epc.mnc435.mcc311.3gppnetwork.org.




* Back ported the HSS_MAP  from https://github.com/open5gs/open5gs/commit/9d83eba550bd693bd18568d60189b14161b97f06 




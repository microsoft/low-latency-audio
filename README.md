# Low-latency Audio Driver

This project includes a new USB Audio Class 2 (UAC2) driver, optimized for low-latency musician scenarios. It also includes an ASIO interface to the UAC2 driver to support applications which use the ASIO standard. This driver, and its supporting components, will be shipped in Windows.

> There is no public release of the driver, yet. **This section will be updated with the Windows Insider Build information once the driver preview ships in Windows.**

This project is open source to enable community participation, and to encourage others to create and ship useful low-latency audio drivers for Windows. Microsoft has a license with Steinberg specifically for the use of ASIO in Windows, when shipped from/by Microsoft for Windows. This license does not transfer to individuals, companies, or organizations building this source code, or deriving other drivers from this source code. Please refer to the Steinberg ASIO SDK license for more information.

### Steinberg ASIO SDK

The projects in this repo require the Steinberg ASIO SDK to compile. Use of the source here does not grant any license for the Steinberg ASIO SDK. All contributors to the project, and anyone compiling from source, is required to have an agreement with Steinberg and obtain the ASIO SDK directly from them. 

In the case that asio SDK files are (incorrectly) included in a pull request or other location in this repo, they are excluded from the MIT license that governs this repo. Nothing in this repo shall be construed as impacting or modifying the Steinberg license agreement for the ASIO SDK.

ASIO is a trademark and software of Steinberg Media Technologies GmbH.

## Acknowledgements

The UAC2 driver and its ASIO components have been developed in partnership with Yamaha Corporation of Japan, and Qualcomm. Thank you to these companies for their continued support for musicians and low-latency audio on Windows.

## Related Projects

The Windows MIDI Services Project https://aka.ms/midi is highly related to this project in that they are two sides of the coin for musician-focused APIs on Windows.

## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us the rights to use your contribution. For details, visit https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Questions and Discussion

For questions, suggestions, and discussion, please join our [MIDI / Audio Discord server](https://aka.ms/mididiscord). If possible, please use your real name when posting questions in the appropriate forums there. Be respectful of others.

For general suggestions not directly related to the low-latency audio project, please use the Microsoft Feedback Hub app.

## Trademarks

ASIO is a trademark and software of Steinberg Media Technologies GmbH.

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).

Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship. Any use of third-party trademarks or logos are subject to those third-party's policies.

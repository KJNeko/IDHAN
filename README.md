# Welcome to IDHAN's git page
IDHAN is a performance first booru/image manager to be run locally on any computer.

Designed from the ground up with modularity in mind IDHAN features many tactics in order to ensure a responsive GUI and optomized backend.

## GUI
IDHAN uses Qt as it's GUI built in a way that allows for a completely user configurable experience. Everything can be moved and modules can be made by advanced users using a C++ api ~~and a Python API~~ soon

## Performance
As a impatient person myself I made sure that IDHAN would run as fast as possible in almost any situation. The database backend uses Postgresql in order to get the absolute maximum performance I could get my hands on. I use many tools in order to monitor performance and find bottlenecks wherever they are present. Performance before features.

## Modularity
IDHAN uses a module system from the ground up. Meaning everything can be replaced. The database currently has two available options, Postgresql and sqlite, For performance and portability respectively. The entire UI is made in a way so that each page has it's own environment that manages the resources for that page only. Everything in IDHAN is built in a way that allows for easy swapping of components. 

## Development
I use a strict set of practices when I develop, Including nearly a paragraph of compiler flags to ensure that I stick to the C++ standard. I use c++20 in IDHAN and when C++23 arrives I will use that too. As a college student my time to develop will varry wildly. Releases might be anywhere from a few days to a month in between depending on the desired features. 

## FAQ
Empty

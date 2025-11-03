# Single header DDNet (0.6) Demo reading/writing library (WIP)

everything except net messages works. if you can figure out why net messages are not working, i would appreciate a pr. It also doesn't work with MSVC for some reason, I hate microsoft.

Right now you can use it either as a cmake submodule e.g. add_directory or just copy paste the single header lib to your project and use it. Don't forget to define DDNET_DEMO_IMPLEMENTATION before including it for the first time.

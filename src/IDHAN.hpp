//
// Created by kj16609 on 8/11/22.
//

#ifndef IDHAN_IDHAN_HPP
#define IDHAN_IDHAN_HPP

class IDHAN
{


    void startup()
    {

    }



    void fireEventLoop()
    {
        ZoneScoped;

    }


public:
    int execute()
    {
        spdlog::info("Starting IDHAN");

        startup();

        while(true)
        {
            fireEventLoop();
        }
    }


};

#endif //IDHAN_IDHAN_HPP

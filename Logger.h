#pragma once
#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <mutex>

class Logger : public std::streambuf {
public:
    Logger();
    ~Logger();

    static Logger& GetInstance();
    void DrawImGuiLogger(bool* p_open = nullptr);
    void Clear();

protected:
    virtual int overflow(int c) override;

private:
    std::stringstream logBuffer;
    std::mutex logMutex;
};

#endif // LOGGER_H
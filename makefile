CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp ./mysql/sql_conn_pool.cpp ./log/log.cpp ./http/http_conn.cpp ./lst_timer/lst_timer.cpp
	$(CXX) -o server -std=c++0x  $^ $(CXXFLAGS) -lpthread -lmysqlclient
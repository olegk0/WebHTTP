CXXFLAGS =	-O2 -g -Wall -fmessage-length=0 -DDEBUG

OBJS =		main.o WebHTTP.o

LIBS = `pkg-config libwebsockets --libs --cflags` -lpthread

TARGET =	test

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS) 

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

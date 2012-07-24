OBJDIR := $(BUILD_TYPE)-$(PLATFORM)

# ------------------------------------------------------------------

SOURCES := \
	PIpcMutex.cpp \
	PIpcBuffer.cpp \
	PIpcServer.cpp \
	PIpcClient.cpp \
	PIpcChannel.cpp \
	PIpcSocketSource.cpp \
	pickle.cpp \
	PIpcMessage.cpp \
	PIpcAsyncCaller.cpp

TARGET_OBJS := $(SOURCES:%.cpp=$(OBJDIR)/%.o)

TARGET_NAME := LunaSysMgrIpc
TARGET_SO := $(OBJDIR)/lib$(TARGET_NAME).so

# ------------------------------------------------------------------

TEST_SOURCES := \
	TestPIpcMutex.cpp \
	TestPIpcMutexSigKill.cpp \
	TestPIpcBuffer.cpp \
	TestIpcServerClient.cpp \
	TestIpcServerClientMultiProcess.cpp \
	TestIpcServerClientMultiProcessSyncMessages.cpp \
	TestIpcServerClientMultiProcessSyncAsyncMessages.cpp \
	TestIpcServerClientMultiProcessSyncAsyncMessagesWithThreading.cpp \
	TestPickle.cpp \
	TestIpcMessage.cpp \
	TestIpcSyncMessages.cpp \
	TestIpcAsyncMessages.cpp \
	TestIpcSyncCallWithinAsyncCallback.cpp

TEST_EXES := $(TEST_SOURCES:%.cpp=$(OBJDIR)/%.exe)

# ------------------------------------------------------------------

ifeq ("$(BUILD_TYPE)", "debug")
FLAGS_OPT := -g -O0 -Wall -Werror
INCLUDES += -I./tests
else
FLAGS_OPT := -g -O3  -Wall -Werror -fno-exceptions -fno-rtti
endif

FLAGS_OPT += -fPIC -fvisibility=hidden

INCLUDES := $(INCLUDES) \
	-I./base \
	-I./ipc \
	`pkg-config --cflags glib-2.0 gthread-2.0`

LIBS := $(LIBS) \
	`pkg-config --libs glib-2.0 gthread-2.0` \
	-lpthread

LOCAL_CFLAGS := $(CFLAGS) $(FLAGS_OPT) -DQT_WEBOS
LOCAL_LFLAGS := $(LDFLAGS) $(FLAGS_OPT)

# ------------------------------------------------------------------

all: setup $(TARGET_OBJS) $(TARGET_SO)

setup:
	@mkdir -p $(OBJDIR)

tests: $(TEST_EXES)

vpath %.cpp ipc base tests

$(TARGET_SO): $(TARGET_OBJS)
	$(CXX) $(LOCAL_LFLAGS) -shared -Wl,--no-undefined -o $(TARGET_SO) $(TARGET_OBJS) $(LIBS)

$(OBJDIR)/%.o: %.cpp
	$(CXX) -MD $(INCLUDES) $(LOCAL_CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.c
	$(CC) -MD $(INCLUDES) $(LOCAL_CFLAGS) -c $< -o $@

$(OBJDIR)/%.exe: %.cpp
	$(CXX) $(LOCAL_LFLAGS) $(TEST_LIBS) $(INCLUDES) $(LOCAL_CFLAGS) $(TEST_CFLAGS) $< -o $@ $(LIBS) \
	-Wl,-rpath $(OBJDIR) -L$(OBJDIR) -l$(TARGET_NAME)

-include $(SOURCES:%.cpp=$(OBJDIR)/%.d)

clean:
	rm -rf $(OBJDIR)


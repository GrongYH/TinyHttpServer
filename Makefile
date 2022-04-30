bin=httpserver
cc=g++
LD_FLAGS=-std=c++11 -lpthread
src=main.cc
CGI=test_cgi

.PHONY:ALL
ALL:$(bin) $(CGI)
$(bin):$(src)
	$(cc) -o $@ $^ $(LD_FLAGS)
$(CGI):test_cgi.cc 	
	$(cc) -o $@ $^ $(LD_FLAGS)
	cp $@ ./wwwroot

.PHONY:clean
clean:
	rm -f $(bin)
	rm -f $(CGI)
	rm -f ./wwwroot/test_cgi

require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/../fixtures/classes'

describe "TCPSocket#gethostbyname" do
  it "should resolve a hostname to an address" do
    a = TCPSocket.gethostbyname("localhost")
    a.first.should == 'localhost'
  end
end

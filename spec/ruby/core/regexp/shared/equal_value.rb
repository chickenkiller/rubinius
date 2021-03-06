describe :regexp_eql, :shared => true do
  it "is true if self and other have the same pattern" do
    /abc/.send(@method, /abc/).should == true
    /abc/.send(@method, /abd/).should == false
  end

  it "is true if self and other have the same character set code" do
    /abc/.send(@method, /abc/x).should == false
    /abc/x.send(@method, /abc/x).should == true
    /abc/u.send(@method, /abc/n).should == false
    /abc/u.send(@method, /abc/u).should == true
    /abc/n.send(@method, /abc/n).should == true
  end

  it "is true if other has the same #casefold? values" do
    /abc/.send(@method, /abc/i).should == false
    /abc/i.send(@method, /abc/i).should == true
  end
end

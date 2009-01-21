/*
 * 
 * 
 * Copyright (C) 2006 SIPfoundry Inc.
 * Licensed by SIPfoundry under the LGPL license.
 * 
 * Copyright (C) 2006 Pingtel Corp.
 * Licensed to SIPfoundry under a Contributor Agreement.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.phone.nortel12x0;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;

import junit.framework.TestCase;

import org.apache.commons.io.IOUtils;
import org.easymock.EasyMock;
import org.easymock.IMocksControl;
import org.sipfoundry.sipxconfig.TestHelper;
import org.sipfoundry.sipxconfig.common.User;
import org.sipfoundry.sipxconfig.device.DeviceDefaults;
import org.sipfoundry.sipxconfig.device.MemoryProfileLocation;
import org.sipfoundry.sipxconfig.device.Profile;
import org.sipfoundry.sipxconfig.device.RestartException;
import org.sipfoundry.sipxconfig.phone.Line;
import org.sipfoundry.sipxconfig.phone.LineInfo;
import org.sipfoundry.sipxconfig.phone.Phone;
import org.sipfoundry.sipxconfig.phone.PhoneContext;
import org.sipfoundry.sipxconfig.phone.PhoneModel;
import org.sipfoundry.sipxconfig.phone.PhoneTestDriver;
import org.sipfoundry.sipxconfig.phone.nortel12x0.Nortel12x0Phone.Nortel12x0Context;
import org.sipfoundry.sipxconfig.phonebook.PhonebookEntry;
import org.sipfoundry.sipxconfig.speeddial.Button;
import org.sipfoundry.sipxconfig.speeddial.SpeedDial;

public class Nortel12x0PhoneTest extends TestCase {

    private final Collection<PhonebookEntry> m_emptyPhonebook = Collections.<PhonebookEntry> emptyList();

    public void _testFactoryRegistered() {
        PhoneContext pc = (PhoneContext) TestHelper.getApplicationContext().getBean(
                PhoneContext.CONTEXT_BEAN_NAME);
        assertNotNull(pc.newPhone(new PhoneModel("nortel12x0")));
    }

    public void testGetFileName() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        phone.setSerialNumber("00041c001e6c");
        assertEquals("Nortel/config/SIP00041C001E6C.xml", phone.getProfileFilename());
    }

    public void testGetSettings() {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);
	PhoneTestDriver.supplyTestData(phone);
        assertNotNull(phone.getSettings());
    }

    public void testExternalLine() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);

        PhoneTestDriver.supplyTestData(phone, new ArrayList<User>());
        LineInfo li = new LineInfo();
        li.setDisplayName("abc xyz");
        li.setUserId("def");
        li.setRegistrationServer("example.org");
        li.setPassword("1234");

        Line line = phone.createLine();
        phone.addLine(line);
        line.setLineInfo(li);

        assertEquals("sip:def@example.org", line.getUri());
    }

    public void testRestart() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);

        PhoneTestDriver testDriver = PhoneTestDriver.supplyTestData(phone);
        phone.restart();

        testDriver.sipControl.verify();
    }

    public void testRestartNoLine() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);

        PhoneTestDriver.supplyTestData(phone, new ArrayList<User>());
        try {
            phone.restart();
            fail();
        } catch (RestartException re) {
            assertTrue(true);
        }
    }


    public void testGenerateTypicalProfile() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);
        PhoneTestDriver.supplyTestData(phone);
        MemoryProfileLocation location = TestHelper.setVelocityProfileGenerator(phone);
        phone.generateProfiles(location);
        InputStream expectedProfile = getClass().getResourceAsStream("expected-config");
        String expected = IOUtils.toString(expectedProfile);
        expectedProfile.close();
	assertEquals(expected, location.toString());
    }

    public void testGenerateProfilesForSpeedDial() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);

        PhoneTestDriver.supplyTestData(phone);
        MemoryProfileLocation location = TestHelper.setVelocityProfileGenerator(phone);

        Button[] buttons = new Button[] {
            new Button("a,b,c", "123@sipfoundry.org"), new Button("def_def", "456"), new Button("xyz abc", "789"),
            new Button(null, "147"), new Button("Joe User", "258@sipfoundry.com"), new Button("", "369")
        };

        SpeedDial sp = new SpeedDial();
        sp.setButtons(Arrays.asList(buttons));

        IMocksControl phoneContextControl = EasyMock.createNiceControl();
        PhoneContext phoneContext = phoneContextControl.createMock(PhoneContext.class);
        PhoneTestDriver.supplyVitalTestData(phoneContextControl, phoneContext, phone);

        phoneContext.getSpeedDial(phone);
        phoneContextControl.andReturn(sp).anyTimes();
        phoneContextControl.replay();

        phone.setPhoneContext(phoneContext);
        phone.generateProfiles(location);

        InputStream expectedProfile = getClass().getResourceAsStream("speed-dials");
        String expected = IOUtils.toString(expectedProfile);
        expectedProfile.close();
	assertEquals(expected, location.toString());
        phoneContextControl.verify();
    }

    public void testGenerateProfilesForPhoneBook() throws Exception {

        Nortel12x0Phone phone = new Nortel12x0Phone();
        PhoneModel model = new PhoneModel("nortel12x0");
        model.setLabel("Nortel IP Phone 1230");
        model.setModelDir("nortel12x0");
        model.setProfileTemplate("nortel12x0/nortel12x0.vm");
        phone.setModel(model);

        PhoneTestDriver.supplyTestData(phone);
        MemoryProfileLocation location = TestHelper.setVelocityProfileGenerator(phone);

        List< ? extends PhonebookEntry> phonebook = Arrays.asList(new DummyEntry("001"),
                new DummyEntry("003"), new DummyEntry("005"));

        IMocksControl phoneContextControl = EasyMock.createNiceControl();
        PhoneContext phoneContext = phoneContextControl.createMock(PhoneContext.class);
        PhoneTestDriver.supplyVitalTestData(phoneContextControl, phoneContext, phone);

        phoneContext.getPhonebookEntries(phone);
        phoneContextControl.andReturn(phonebook).anyTimes();
        phoneContextControl.replay();

        phone.setPhoneContext(phoneContext);
        phone.generateProfiles(location);

        InputStream expectedProfile = getClass().getResourceAsStream("phonebook");
        String expected = IOUtils.toString(expectedProfile);
        expectedProfile.close();
	assertEquals(expected, location.toString());

        phoneContextControl.verify();
    }

    public void testNortel12x0ContextEmpty() {

        Nortel12x0Phone phone = new Nortel12x0Phone();

        Nortel12x0Context sc = new Nortel12x0Phone.Nortel12x0Context(phone, null, m_emptyPhonebook, null);
        String[] numbers = (String[]) sc.getContext().get("speedDialInfo");

        assertEquals(0, numbers.length);
    }

    public void testNortel12x0MaxNumOfSpeedDials() {

        SpeedDial maxSd = createSpeedDial(220);

        Nortel12x0Phone phone = new Nortel12x0Phone();

        Nortel12x0Context sc = new Nortel12x0Phone.Nortel12x0Context(phone, maxSd, m_emptyPhonebook, null);
        String[] numbers = (String[]) sc.getContext().get("speedDialInfo");
        assertEquals(200 * 3, numbers.length);

    }

    public void testNortel12x0ActualNumOfSpeedDialsGenerated() {

        SpeedDial smallSd = createSpeedDial(5);
        SpeedDial largeSd = createSpeedDial(100);

        Nortel12x0Phone phone = new Nortel12x0Phone();

        Nortel12x0Context sc = new Nortel12x0Phone.Nortel12x0Context(phone, smallSd, m_emptyPhonebook, null);
        String[] numbers = (String[]) sc.getContext().get("speedDialInfo");
        assertEquals(5 * 3, numbers.length);

        sc = new Nortel12x0Phone.Nortel12x0Context(phone, largeSd, m_emptyPhonebook, null);
        numbers = (String[]) sc.getContext().get("speedDialInfo");
        assertEquals(100 * 3, numbers.length);
    }

    public void testNortel12x0MaxNumOfPhonebookEntries() {

        Collection<PhonebookEntry> phonebook = new ArrayList<PhonebookEntry>();

        for (int i = 0; i < 220; i++) {
            phonebook.add(new DummyEntry(Integer.toString(i)));
        }

        Nortel12x0Phone phone = new Nortel12x0Phone();

        Nortel12x0Context sc = new Nortel12x0Phone.Nortel12x0Context(phone, null, phonebook, null);
        Collection<?> maxEntries = (Collection<?>) sc.getContext().get("phoneBook");
        assertEquals(200, maxEntries.size());
    }

    public void testNortel12x0ActualNumOfPhoneBookEntriesConfigured() {

        Collection<PhonebookEntry> phonebook = new ArrayList<PhonebookEntry>();

        for (int i = 0; i < 120; i++) {
            phonebook.add(new DummyEntry(Integer.toString(i)));
        }

        Nortel12x0Phone phone = new Nortel12x0Phone();

        Nortel12x0Context sc = new Nortel12x0Phone.Nortel12x0Context(phone, null, phonebook, null);
        Collection<?> maxEntries = (Collection<?>) sc.getContext().get("phoneBook");
        assertEquals(120, maxEntries.size());
    }

    private SpeedDial createSpeedDial(int size) {

        List<Button> buttons = new ArrayList<Button>();
        for (int i = 0; i < size; i++) {
            buttons.add(new Button("test", Integer.toString(i)));
        }
        SpeedDial sd = new SpeedDial();
        sd.setButtons(buttons);
        return sd;
    }

    private void supplyTestData(Nortel12x0Phone phone) {
        User u1 = new User();
        u1.setUserName("juser");
        u1.setFirstName("Joe");
        u1.setLastName("User");
        u1.setSipPassword("1234");

        User u2 = new User();
        u2.setUserName("buser");
        u2.setSipPassword("abcdef");
        u2.addAlias("432");

        // call this to inject dummy data
        PhoneTestDriver.supplyTestData(phone, Arrays.asList(new User[] {
		     u1, u2
        }));
    }

    static class DummyEntry implements PhonebookEntry {

        private String entry;

        public DummyEntry(String param) {
            entry = param;
        }

        public String getFirstName() {
            return "bookFirstName" + entry;
        }

        public String getLastName() {
            return "bookLastName" + entry;
        }

        public String getNumber() {
            return "bookNumber" + entry;
        }

    }

}

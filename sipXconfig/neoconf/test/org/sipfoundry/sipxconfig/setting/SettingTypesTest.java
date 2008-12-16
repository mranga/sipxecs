/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.setting;

import java.io.File;
import java.util.Iterator;
import java.util.Map;

import junit.framework.TestCase;

import org.sipfoundry.sipxconfig.TestHelper;
import org.sipfoundry.sipxconfig.setting.type.BooleanSetting;
import org.sipfoundry.sipxconfig.setting.type.EnumSetting;
import org.sipfoundry.sipxconfig.setting.type.FileSetting;
import org.sipfoundry.sipxconfig.setting.type.IntegerSetting;
import org.sipfoundry.sipxconfig.setting.type.RealSetting;
import org.sipfoundry.sipxconfig.setting.type.SettingType;
import org.sipfoundry.sipxconfig.setting.type.SipUriSetting;
import org.sipfoundry.sipxconfig.setting.type.StringSetting;

public class SettingTypesTest extends TestCase {

    private Setting group;

    @Override
    protected void setUp() throws Exception {
        ModelBuilder builder = new XmlModelBuilder("etc");
        File in = TestHelper.getResourceAsFile(getClass(), "setting-types.xml");
        SettingSet root = builder.buildModel(in);
        group = root.getSetting("group");
    }

    public void testSettingIntegerType() {
        final int[][] EXPECTED = {
            {
                3, 15
            }, {
                0, Integer.MAX_VALUE
            }
        };

        final boolean[] EXPECTED_REQUIRED = {
            true, false
        };

        for (int i = 0; i < EXPECTED.length; i++) {
            int[] min_max = EXPECTED[i];

            Setting intSetting = group.getSetting("int_setting_" + i);
            SettingType type = intSetting.getType();
            assertTrue(type instanceof IntegerSetting);
            IntegerSetting intType = (IntegerSetting) type;
            assertEquals(min_max[0], intType.getMin());
            assertEquals(min_max[1], intType.getMax());
            assertEquals(EXPECTED_REQUIRED[i], intType.isRequired());
            assertTrue(intSetting.getTypedValue() instanceof Integer);

            assertEquals(intSetting.getTypedValue().toString(), intSetting.getValue());
        }
    }

    public void testNullSettings() {
        SettingType[] types = {
            new IntegerSetting(), new RealSetting(), new BooleanSetting(), new StringSetting(), new EnumSetting()
        };

        for (int i = 0; i < types.length; i++) {
            Setting setting = new SettingImpl();
            SettingType type = types[i];

            setting.setType(type);
            assertNull("Failed for" + type, setting.getTypedValue());
        }
    }

    public void testSettingRealType() {
        final float[][] EXPECTED = {
            {
                3.14f, 15.12f
            }, {
                0, Float.MAX_VALUE
            }
        };

        final boolean[] EXPECTED_REQUIRED = {
            true, false
        };

        for (int i = 0; i < EXPECTED.length; i++) {
            float[] min_max = EXPECTED[i];

            Setting realSetting = group.getSetting("real_setting_" + i);
            SettingType type = realSetting.getType();
            assertTrue(type instanceof RealSetting);
            RealSetting realType = (RealSetting) type;
            assertEquals(min_max[0], realType.getMin(), 0.001f);
            assertEquals(min_max[1], realType.getMax(), 0.001f);
            assertEquals(EXPECTED_REQUIRED[i], realType.isRequired());

            assertTrue(realSetting.getTypedValue() instanceof Double);

            assertEquals(realSetting.getTypedValue().toString(), realSetting.getValue());
        }
    }

    public void testSettingStringType() {
        Setting stringSetting = group.getSetting("str_setting_def");
        SettingType type = stringSetting.getType();
        assertTrue(type instanceof StringSetting);
        StringSetting strType = (StringSetting) type;
        assertEquals(256, strType.getMaxLen());
        assertNull(strType.getPattern());
        assertFalse(strType.isRequired());
        assertFalse(strType.isPassword());

        stringSetting = group.getSetting("str_setting");
        type = stringSetting.getType();
        assertTrue(type instanceof StringSetting);
        strType = (StringSetting) type;
        assertEquals(3, strType.getMinLen());
        assertEquals(15, strType.getMaxLen());
        assertEquals("kuku", strType.getPattern());
        assertTrue(strType.isRequired());
        assertTrue(strType.isPassword());

        assertTrue(stringSetting.getTypedValue() instanceof String);

        assertSame(stringSetting.getTypedValue().toString(), stringSetting.getValue());
    }

    public void testSettingEnumType() {
        final String[][] V2L = {
            {
                "0", "Zero"
            }, {
                "something", "XXX"
            }, {
                "no_label_value", "no_label_value"
            }
        };
        Setting enumSetting = group.getSetting("enum_setting");
        assertNotNull(enumSetting);
        SettingType type = enumSetting.getType();
        assertTrue(type instanceof EnumSetting);
        EnumSetting enumType = (EnumSetting) type;
        Map enums = enumType.getEnums();
        assertEquals(V2L.length, enums.size());
        int i = 0;
        for (Iterator iter = enums.entrySet().iterator(); iter.hasNext(); i++) {
            Map.Entry entry = (Map.Entry) iter.next();
            assertEquals(V2L[i][0], entry.getKey());
            assertEquals(V2L[i][1], entry.getValue());
        }

        assertTrue(enumSetting.getTypedValue() instanceof Integer);
        assertEquals(new Integer(0), enumSetting.getTypedValue());
    }

    public void testDefaultBooleanType() {
        Setting setting = group.getSetting("boolean_default");
        SettingType type = setting.getType();

        assertTrue(type instanceof BooleanSetting);
        BooleanSetting boolSetting = (BooleanSetting) type;
        assertEquals("1", boolSetting.getTrueValue());
        assertEquals("0", boolSetting.getFalseValue());

        assertTrue(setting.getTypedValue() instanceof Boolean);

        assertSame(setting.getTypedValue(), Boolean.TRUE);
    }

    public void testBooleanType() {
        Setting setting = group.getSetting("boolean_setting");
        SettingType type = setting.getType();

        assertTrue(type instanceof BooleanSetting);
        BooleanSetting boolSetting = (BooleanSetting) type;
        assertEquals("true", boolSetting.getTrueValue());
        assertEquals("false", boolSetting.getFalseValue());

        assertTrue(setting.getTypedValue() instanceof Boolean);

        assertSame(setting.getTypedValue(), Boolean.FALSE);
    }

    public void testBooleanTypeWithLabels() {
        Setting setting = group.getSetting("enabled_setting");
        assertEquals(Boolean.FALSE, setting.getTypedValue());
        setting.setValue("ENABLED");
        assertEquals(Boolean.TRUE, setting.getTypedValue());
    }

    public void testFileType() {
        Setting setting = group.getSetting("file_setting");
        SettingType type = setting.getType();

        assertTrue(type instanceof FileSetting);
        FileSetting fileType = (FileSetting) type;
        assertTrue(fileType.isRequired());
        assertEquals("/tmp", fileType.getDirectory());
        assertEquals("audio/x-wav", fileType.getContentType());
    }

    public void testGetEnumsWithNoLabels() {
        Setting setting = group.getSetting("enum_setting_2");
        EnumSetting enums = (EnumSetting) setting.getType();
        assertEquals(3, enums.getEnums().size());
    }

    public void testGetEnumTypeValue() {
        Setting setting = group.getSetting("enum_setting_2");
        Object value = setting.getTypedValue();
        assertEquals("VALUE_0", value);

        setting.setValue("VALUE_2");
        Object value2 = setting.getTypedValue();
        assertEquals("VALUE_2", value2);

        EnumSetting type = (EnumSetting) setting.getType();
        assertEquals("group.enum_setting_2.label.VALUE_2", type.getLabelKey(setting, "VALUE_2"));
    }

    public void testGetEnumTypeLabelKeyPrefix() {
        Setting setting = group.getSetting("enum_setting_3");
        Object value = setting.getTypedValue();
        assertEquals("VALUE_0", value);

        setting.setValue("VALUE_2");
        Object value2 = setting.getTypedValue();
        assertEquals("VALUE_2", value2);

        EnumSetting type = (EnumSetting) setting.getType();
        assertEquals("type.enum3.VALUE_2", type.getLabelKey(setting, "VALUE_2"));
    }

    public void testSettingSipUriType() {
        Setting stringSetting = group.getSetting("sip_uri_setting");
        SettingType type = stringSetting.getType();
        assertTrue(type instanceof SipUriSetting);
        SipUriSetting strType = (SipUriSetting) type;
        assertEquals(256, strType.getMaxLen());
        assertNotNull(strType.getPattern());
        assertFalse(strType.getPattern().contains("@"));
        assertTrue(strType.isRequired());
        assertFalse(strType.isPassword());
    }
}

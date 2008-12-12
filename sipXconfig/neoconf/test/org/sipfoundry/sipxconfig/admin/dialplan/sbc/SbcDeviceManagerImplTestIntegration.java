/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 *
 */
package org.sipfoundry.sipxconfig.admin.dialplan.sbc;

import java.util.Collection;

import org.sipfoundry.sipxconfig.IntegrationTestCase;
import org.sipfoundry.sipxconfig.admin.dialplan.sbc.bridge.BridgeSbc;
import org.sipfoundry.sipxconfig.common.UserException;
import org.sipfoundry.sipxconfig.device.ModelSource;
import org.sipfoundry.sipxconfig.nattraversal.NatTraversalManager;

public class SbcDeviceManagerImplTestIntegration extends IntegrationTestCase {
    private SbcDeviceManager m_sdm;
    private NatTraversalManager m_natManager;

    private ModelSource<SbcDescriptor> m_modelSource;

    public void testNewSbc() {
        Collection<SbcDescriptor> models = m_modelSource.getModels();
        // at least generic model is empty
        assertFalse(models.isEmpty());
        for (SbcDescriptor model : models) {
            SbcDevice newSbcDevice = m_sdm.newSbcDevice(model);
            assertEquals(newSbcDevice.getBeanId(), model.getBeanId());
            assertEquals(newSbcDevice.getModelId(), model.getModelId());
            assertSame(newSbcDevice.getModel(), model);
        }
    }

    public void testClear() {
        loadDataSet("admin/dialplan/sbc/sbc-device.db.xml");
        assertEquals(4, countRowsInTable("sbc_device"));
        m_sdm.clear();
        flush();

        assertEquals(0, countRowsInTable("sbc_device"));
    }

    public void testGetAllSbcDeviceIds() {
        loadDataSet("admin/dialplan/sbc/sbc-device.db.xml");
        Collection<Integer> allSbcDeviceIds = m_sdm.getAllSbcDeviceIds();
        assertEquals(4, allSbcDeviceIds.size());
        assertTrue(allSbcDeviceIds.contains(1000));
        assertTrue(allSbcDeviceIds.contains(1001));
        assertTrue(allSbcDeviceIds.contains(1002));
        assertTrue(allSbcDeviceIds.contains(1003));
    }

    public void testGetSbcDevice() {
        loadDataSet("admin/dialplan/sbc/sbc-device.db.xml");
        SbcDevice sbc = m_sdm.getSbcDevice(1001);

        assertEquals(Integer.valueOf(1001), sbc.getId());
        assertEquals("Sbc1001", sbc.getName());
        assertEquals("10.1.2.4", sbc.getAddress());
        assertEquals("101122334455", sbc.getSerialNumber());
        assertEquals("description1", sbc.getDescription());
    }

    public void testGetSbcBridge() {
        loadDataSet("admin/dialplan/sbc/sbc-device.db.xml");
        BridgeSbc bridgeSbc = m_sdm.getBridgeSbc();
        assertEquals("SbcBridge", bridgeSbc.getName());
        assertEquals("10.1.2.6", bridgeSbc.getAddress());
        assertEquals("301122334455", bridgeSbc.getSerialNumber());
        assertEquals("bridgeDesc", bridgeSbc.getDescription());
        m_sdm.deleteSbcDevice(1003);
        bridgeSbc = m_sdm.getBridgeSbc();
        assertTrue(bridgeSbc==null);
    }

    public void testSave() {
        SbcDescriptor model = m_modelSource.getModel("sbcGenericModel");
        SbcDevice newSbcDevice = m_sdm.newSbcDevice(model);
        m_sdm.storeSbcDevice(newSbcDevice);
        assertFalse(newSbcDevice.isNew());
        flush();

        assertEquals(1, countRowsInTable("sbc_device"));

        newSbcDevice.setName("abc");
        m_sdm.storeSbcDevice(newSbcDevice);
    }

    public void testSaveErrorMaxAllowed() {
        m_natManager.saveDefaultNatTraversal();
        SbcDescriptor model = m_modelSource.getModel("sipXbridgeSbcModel");
        model.setMaxAllowed(1);
        SbcDevice newSbcDevice = m_sdm.newSbcDevice(model);
        newSbcDevice.setName("aaa");
        m_sdm.storeSbcDevice(newSbcDevice);
        flush();
        try {
            newSbcDevice = m_sdm.newSbcDevice(model);
            newSbcDevice.setName("bbb");
            m_sdm.storeSbcDevice(newSbcDevice);
            fail("Creation of two bridges should not be accepted.");
        } catch (UserException e) {
            // ok - 2 bridges failed
        }
    }

    public void testSaveDuplicate() {
        SbcDescriptor model = m_modelSource.getModel("sbcGenericModel");
        SbcDevice newSbcDevice = m_sdm.newSbcDevice(model);
        newSbcDevice.setName("aaa");
        m_sdm.storeSbcDevice(newSbcDevice);
        flush();

        try {
            newSbcDevice = m_sdm.newSbcDevice(model);
            newSbcDevice.setName("aaa");
            m_sdm.storeSbcDevice(newSbcDevice);
            fail("Should not accept duplicated name.");
        } catch (UserException e) {
            // ok - duplicate name
        }
    }

    public void testMaxAllowedLimitReached() {
        loadDataSet("admin/dialplan/sbc/sbc-device.db.xml");
        SbcDescriptor bridgeModel = m_modelSource.getModel("sipXbridgeSbcModel");
        assertTrue(m_sdm.maxAllowedLimitReached(bridgeModel));
        SbcDescriptor sbcModel = m_modelSource.getModel("sbcGenericModel");
        assertFalse(m_sdm.maxAllowedLimitReached(sbcModel));
    }

    public void setSbcDeviceManager(SbcDeviceManager sdm) {
        m_sdm = sdm;
    }

    public void setNatTraversalManager(NatTraversalManager natManager) {
        m_natManager = natManager;
    }

    public void setSbcModelSource(ModelSource<SbcDescriptor> modelSource) {
        m_modelSource = modelSource;
    }
}

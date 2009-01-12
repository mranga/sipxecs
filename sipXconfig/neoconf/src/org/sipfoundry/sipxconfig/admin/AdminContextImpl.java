/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.admin;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.Writer;
import java.util.List;
import java.util.TimeZone;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.sipfoundry.sipxconfig.admin.ftp.FtpConfiguration;
import org.sipfoundry.sipxconfig.common.ApplicationInitializedEvent;
import org.sipfoundry.sipxconfig.common.DSTChangeEvent;
import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.BeanFactoryAware;
import org.springframework.context.ApplicationEvent;
import org.springframework.context.ApplicationListener;
import org.springframework.dao.support.DataAccessUtils;
import org.springframework.orm.hibernate3.support.HibernateDaoSupport;

import static org.springframework.dao.support.DataAccessUtils.singleResult;

/**
 * Backup provides Java interface to backup scripts
 */
public abstract class AdminContextImpl extends HibernateDaoSupport implements AdminContext, ApplicationListener,
        BeanFactoryAware {
    private static final String DATE_BINARY = "sipx-sudo-date";
    private static final String TIMEZONE_BINARY = "sipx-sudo-timezone";

    private static final Log LOG = LogFactory.getLog(AdminContextImpl.class);

    private String m_binDirectory;

    private String m_libExecDirectory;

    private ExportCsv m_exportCsv;

    private BeanFactory m_beanFactory;

    public abstract FtpBackupPlan createFtpBackupPlan();

    public abstract LocalBackupPlan createLocalBackupPlan();

    public String getBinDirectory() {
        return m_binDirectory;
    }

    public void setBinDirectory(String binDirectory) {
        m_binDirectory = binDirectory;
    }

    public String getLibExecDirectory() {
        return m_libExecDirectory;
    }

    public void setLibExecDirectory(String libExecDirectory) {
        m_libExecDirectory = libExecDirectory;
    }

    public void setExportCsv(ExportCsv exportCsv) {
        m_exportCsv = exportCsv;
    }

    public BackupPlan getBackupPlan(String type) {
        boolean isFtp = FtpBackupPlan.TYPE.equals(type);
        Class klass = isFtp ? FtpBackupPlan.class : LocalBackupPlan.class;
        List plans = getHibernateTemplate().loadAll(klass);
        BackupPlan plan = (BackupPlan) DataAccessUtils.singleResult(plans);
        if (plan == null) {
            plan = isFtp ? createFtpBackupPlan() : createLocalBackupPlan();
            if (isFtp) {
                initFtpConfig(plan);
            }
            getHibernateTemplate().save(plan);
            getHibernateTemplate().flush();
        }
        return plan;
    }

    private void initFtpConfig(BackupPlan plan) {
        FtpBackupPlan ftpBackupPlan = (FtpBackupPlan) plan;
        if (ftpBackupPlan.getFtpConfiguration() != null) {
            return;
        }
        List ftpConfigs = getHibernateTemplate().loadAll(FtpConfiguration.class);
        FtpConfiguration ftpConfig = (FtpConfiguration) singleResult(ftpConfigs);
        if (ftpConfig == null) {
            ftpConfig = new FtpConfiguration();
        }
        ftpBackupPlan.setFtpConfiguration(ftpConfig);
    }

    public void storeBackupPlan(BackupPlan plan) {
        getHibernateTemplate().saveOrUpdate(plan);
        plan.resetTimer(m_binDirectory);
    }

    public File[] performBackup(BackupPlan plan) {
        return plan.perform(m_binDirectory);
    }

    public void performExport(Writer writer) throws IOException {
        m_exportCsv.exportCsv(writer);
    }

    /**
     * start backup timers after app is initialized
     */
    public void onApplicationEvent(ApplicationEvent event) {
        // No need to register listener, all beans that implement listener
        // interface are
        // automatically registered
        if (event instanceof ApplicationInitializedEvent || event instanceof DSTChangeEvent) {
            List<BackupPlan> plans = getHibernateTemplate().loadAll(BackupPlan.class);
            for (BackupPlan plan : plans) {
                plan.resetTimer(m_binDirectory);
            }
        }
    }

    public String[] getInitializationTasks() {
        List l = getHibernateTemplate().findByNamedQuery("taskNames");
        return (String[]) l.toArray(new String[l.size()]);
    }

    public void deleteInitializationTask(String task) {
        List l = getHibernateTemplate().findByNamedQueryAndNamedParam("taskByName", "task", task);
        getHibernateTemplate().deleteAll(l);
    }

    public void setBeanFactory(BeanFactory beanFactory) {
        m_beanFactory = beanFactory;
    }

    public boolean inUpgradePhase() {
        // HACK: need to find a better way of finding out if this is upgrade or normal (tapestry)
        // for now we are assuming that "tapestry" bean is not available during upgrade run
        return !m_beanFactory.containsBean("tapestry");
    }

    public void setSystemDate(String dateStr) {
        String errorMsg = "Error when changing date";
        ProcessBuilder pb = new ProcessBuilder(getLibExecDirectory() + File.separator + DATE_BINARY);

        pb.command().add(dateStr);
        try {
            LOG.debug(pb.command());
            Process process = pb.start();
            BufferedReader scriptErrorReader = new BufferedReader(new InputStreamReader(process.getErrorStream()));
            String errorLine = scriptErrorReader.readLine();
            while (errorLine != null) {
                LOG.warn("sipx-sudo-date: " + errorLine);
                errorLine = scriptErrorReader.readLine();
            }
            int code = process.waitFor();
            if (code != 0) {
                errorMsg = String.format("Error when changing date. Exit code: %d", code);
                LOG.error(errorMsg);
            }
        } catch (IOException e) {
            LOG.error(errorMsg, e);
        } catch (InterruptedException e) {
            LOG.error(errorMsg, e);
        }
    }

    public void setSystemTimezone(String timezone) {

        String errorMsg = "Error when changing time zone";
        ProcessBuilder pb = new ProcessBuilder(getLibExecDirectory() + File.separator + TIMEZONE_BINARY);
        pb.command().add(timezone);
        try {
            LOG.debug(pb.command());
            Process process = pb.start();
            BufferedReader scriptErrorReader = new BufferedReader(new InputStreamReader(process.getErrorStream()));
            String errorLine = scriptErrorReader.readLine();
            while (errorLine != null) {
                LOG.warn("sipx-sudo-timezone: " + errorLine);
                errorLine = scriptErrorReader.readLine();
            }
            int code = process.waitFor();
            if (code != 0) {
                errorMsg = String.format("Error when changing time zone. Exit code: %d", code);
                LOG.error(errorMsg);
            }
            TimeZone tz = TimeZone.getTimeZone(timezone);
            TimeZone.setDefault(tz);
        } catch (IOException e) {
            LOG.error(errorMsg, e);
        } catch (InterruptedException e) {
            LOG.error(errorMsg, e);
        }
    }

}

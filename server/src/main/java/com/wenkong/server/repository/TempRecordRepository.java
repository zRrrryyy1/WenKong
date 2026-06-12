package com.wenkong.server.repository;

import com.wenkong.server.model.TempRecord;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface TempRecordRepository extends JpaRepository<TempRecord, Long> {

    List<TempRecord> findByDeviceIdAndTimestampBetweenOrderByTimestampAsc(
            String deviceId, LocalDateTime start, LocalDateTime end);

    List<TempRecord> findTop60ByDeviceIdOrderByTimestampDesc(String deviceId);

    long countByDeviceId(String deviceId);
}

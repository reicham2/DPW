<template>
	<PublicPageLayout :label="title">
		<article class="legal-document">
			<h1>{{ title }}</h1>

			<template v-if="kind === 'impressum'">
				<section>
					<h2>Betreiberin</h2>
					<p>
						Pfadi Hünenberg (Pfadi Hü)<br />
						Pfadiheim Hünenberg<br />
						Rigistrasse 1<br />
						6331 Hünenberg<br />
						Schweiz
					</p>
					<p>
						E-Mail:
						<a href="mailto:al@pfadihue.ch">al@pfadihue.ch</a>
					</p>
				</section>

				<section>
					<h2>Inhalt und Kontakt</h2>
					<p>
						DPWeb unterstützt die Planung von Pfadiaktivitäten und stellt
						Funktionen für öffentliche Aktivitätsfreigaben und Formulare bereit.
						Anfragen zur Anwendung kannst du an die oben genannte Kontaktadresse
						richten.
					</p>
				</section>
			</template>

			<template v-else>
				<p class="legal-draft-note">
					Diese Datenschutzhinweise sind ein Organisationsentwurf und keine
					Rechtsberatung. Die verantwortliche Stelle muss sie vor dem öffentlichen
					Einsatz prüfen und mit der tatsächlichen Konfiguration dieser DPWeb-Instanz
					abgleichen.
				</p>

				<section>
					<h2>Verantwortliche Stelle</h2>
					<p>
						Pfadi Hünenberg (Pfadi Hü)<br />
						Pfadiheim Hünenberg<br />
						Rigistrasse 1, 6331 Hünenberg, Schweiz<br />
						<a href="mailto:al@pfadihue.ch">al@pfadihue.ch</a>
					</p>
				</section>

				<section>
					<h2>Welche Daten DPWeb verarbeitet</h2>
					<p>
						DPWeb dient der Planung und Verwaltung von Aktivitäten. Dazu gehören
						Benutzerangaben wie Microsoft-Kennung, E-Mail-Adresse und Anzeigename,
						sowie Aktivitäts-, Programm-, Verantwortlichkeits- und Materialangaben,
						Notizen, Anhänge und eingefügte Bilder. Benutzerkonten enthalten
						zusätzlich Rollen, Abteilungen und Einstellungen für die Nutzung und
						Benachrichtigungen.
					</p>
					<p>
						Bei einer öffentlichen Formularantwort speichert DPWeb die eingegebenen
						Antworten und den Absendezeitpunkt. Zusätzlich werden, sofern vom
						vorgeschalteten Server mitgeliefert, die IP-Adresse sowie der
						Browser-User-Agent erfasst. Bitte übermittle nur Angaben, die für das
						jeweilige Formular erforderlich sind.
					</p>
					<p>
						Bei angemeldeten Benutzern protokolliert das Audit-Log bestimmte
						Änderungen mit Benutzerkennung, Zeitpunkt, Aktion und begrenzten
						Metadaten. Es enthält keine vollständigen Formularantworten oder
						Nachrichteninhalte. Die Standard-Aufbewahrungsdauer beträgt 14 Tage
						und kann durch Administratoren angepasst werden.
					</p>
				</section>

				<section>
					<h2>Öffentliche Freigaben und Formulare</h2>
					<p>
						Geteilte Aktivitäten sind für alle Personen mit dem Freigabelink ohne
						Anmeldung sichtbar. Das umfasst die für die Aktivität freigegebenen
						Inhalte und Programmbilder. Öffentliche Anmelde- und Abmeldeformulare
						können ebenfalls ohne Anmeldung aufgerufen und ausgefüllt werden.
						Teile Links deshalb nur mit den vorgesehenen Empfängerinnen und
						Empfängern.
					</p>
				</section>

				<section>
					<h2>Empfänger und optionale Dienste</h2>
					<p>
						Auf gespeicherte Aktivitäten und Formularantworten können die
						entsprechend berechtigten DPWeb-Benutzer zugreifen. Microsoft Entra ID
						wird für die Anmeldung verwendet. Beim E-Mailversand mit Microsoft
						Graph werden Absender, Empfänger, Betreff und Inhalt der E-Mail an
						Microsoft übermittelt. Für die Kontaktsuche kann DPWeb über Microsoft
						Graph auf Verzeichnisangaben zugreifen.
					</p>
					<p>
						Weitere Integrationen sind optional und werden nur verwendet, wenn
						sie von der Betreiberin eingerichtet und genutzt werden: MiData für
						Stufen- und Mitgliederabfragen, WordPress/EventON für die
						Veröffentlichung von Veranstaltungen sowie GitHub für ausdrücklich
						abgesendete Fehlerberichte. Ein Fehlerbericht kann die Beschreibung,
						die aktuelle Anwendungsadresse, Browserangaben und technische
						Debug-Informationen enthalten; bei einer Anmeldung können auch Name
						und E-Mail-Adresse des meldenden Kontos enthalten sein.
					</p>
					<p>
						Die DPWeb-Instanz wird in der Schweiz von LKlaus Hosting gehostet.
						DPWeb speichert Anwendungsdaten in der für diese Instanz
						eingerichteten Datenbank. Die konkrete Aufbewahrung von
						Serverprotokollen und Sicherungskopien richtet sich nach der
						Konfiguration der Instanz und des Hosting-Anbieters.
					</p>
					<p>
						Die Speicher- und Übermittlungsorte bei Microsoft und anderen
						optional verwendeten Diensten richten sich nach den jeweiligen
						Diensten und der Konfiguration. DPWeb legt diese Orte nicht fest.
					</p>
				</section>

				<section>
					<h2>Speicherung und Löschung</h2>
					<p>
						Formularantworten werden nicht automatisch nach einer festen Frist
						gelöscht. Berechtigte Benutzer können einzelne Antworten löschen;
						Antworten werden auch mit dem endgültigen Löschen des zugehörigen
						Formulars oder der Aktivität entfernt. Audit-Einträge werden
						standardmässig nach 14 Tagen bereinigt; die Frist ist durch
						Administratoren konfigurierbar.
					</p>
					<p>
						DPWeb definiert keine allgemeine Aufbewahrungsfrist für
						Anwendungsdaten, Serverprotokolle oder Sicherungskopien. Deren
						Aufbewahrung hängt von der Verwaltung und Konfiguration dieser
						Instanz ab. Bei Fragen zu Auskunft, Berichtigung oder Löschung deiner
						Daten wende dich an die oben genannte Kontaktadresse.
					</p>
				</section>

				<section>
					<h2>Browser-Speicher und Cookies</h2>
					<p>
						Die Microsoft-Anmeldung speichert ihren Token-Cache im lokalen
						Browser-Speicher. DPWeb verwendet den Browser-Speicher ausserdem für
						Einstellungen und Bearbeitungsentwürfe; einzelne Ansichten verwenden
						den Sitzungsspeicher. Im DPWeb-Frontend ist kein Analyse- oder
						Marketing-Tracking eingebaut. Die Microsoft-Anmeldung ist nicht für
						die Speicherung ihres Zustands in Cookies konfiguriert.
					</p>
					<p>
						Wenn Web-Push-Benachrichtigungen eingerichtet sind, speichert DPWeb
						das Browser-Abonnement und sendet Mitteilungstext und Link an den
						Push-Dienst, den der Browser vorgibt.
					</p>
				</section>

				<section>
					<h2>Kontakt und Aktualisierungen</h2>
					<p>
						Fragen zu diesen Hinweisen oder zu deinen Daten kannst du an
						<a href="mailto:al@pfadihue.ch">al@pfadihue.ch</a> richten.
						Die Hinweise können angepasst werden, wenn sich DPWeb oder die
						Konfiguration dieser Instanz ändert.
					</p>
				</section>
			</template>
		</article>
	</PublicPageLayout>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue';
import PublicPageLayout from '../components/PublicPageLayout.vue';

const props = defineProps<{ kind: 'impressum' | 'datenschutz' }>();
const title = computed(() => props.kind === 'impressum' ? 'Impressum' : 'Datenschutzerklärung');

onMounted(() => {
	document.title = `${title.value} – DPWeb`;
});
</script>

<style scoped>
.legal-document {
	max-width: 760px;
	margin: 0 auto;
	padding: 28px 32px;
	background: var(--card-bg);
	border: 1px solid var(--card-border);
	border-radius: 12px;
	box-shadow: 0 1px 6px rgba(0, 0, 0, 0.06);
}

.legal-document h1 {
	margin: 0 0 24px;
	font-size: clamp(1.7rem, 5vw, 2.2rem);
	line-height: 1.2;
}

.legal-document h2 {
	margin: 24px 0 8px;
	font-size: 1.15rem;
	line-height: 1.35;
}

.legal-document p {
	margin: 0 0 12px;
	color: var(--text-secondary);
	overflow-wrap: anywhere;
}

.legal-document a {
	color: var(--accent);
	text-underline-offset: 0.16em;
}

.legal-document a:focus-visible {
	outline: 2px solid var(--accent);
	outline-offset: 3px;
	border-radius: 2px;
}

.legal-draft-note {
	padding: 12px 14px;
	background: var(--bg-hover);
	border-left: 3px solid var(--accent);
	border-radius: 4px;
}

@media (max-width: 599px) {
	.legal-document {
		padding: 20px 18px;
	}
}
</style>
